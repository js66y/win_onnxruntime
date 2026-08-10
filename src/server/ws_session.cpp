#include "server/ws_session.h"

#include <boost/asio/post.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <nlohmann/json.hpp>

#include "common/log.h"
#include "dialog/sentence_splitter.h"

namespace echo::server {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
using json = nlohmann::json;

namespace {

constexpr uint8_t kFrameMicAudio = 0x01;  // 上行: 麦克风 PCM16 16kHz
constexpr uint8_t kFrameTtsAudio = 0x02;  // 下行: TTS PCM16
constexpr size_t kFrameHeaderSize = 4;

// float [-1,1] -> 二进制下行帧([type][3B 保留][PCM16LE])
std::string EncodeTtsFrame(const AudioBuffer& audio) {
  std::string frame(kFrameHeaderSize + audio.samples.size() * 2, '\0');
  frame[0] = static_cast<char>(kFrameTtsAudio);
  auto* pcm = reinterpret_cast<int16_t*>(frame.data() + kFrameHeaderSize);
  for (size_t i = 0; i < audio.samples.size(); ++i) {
    const float clamped = std::clamp(audio.samples[i], -1.0f, 1.0f);
    pcm[i] = static_cast<int16_t>(clamped * 32767.0f);
  }
  return frame;
}

}  // namespace

WsSession::WsSession(beast::tcp_stream stream, ChatService& chat,
                     SpeechService* speech)
    : ws_(std::move(stream)), chat_(chat), speech_(speech) {}

WsSession::~WsSession() {
  if (mic_acquired_ && speech_) speech_->ReleaseMic(this);
}

void WsSession::Run(
    beast::http::request<beast::http::string_body> req) {
  beast::get_lowest_layer(ws_).expires_never();
  ws_.set_option(
      websocket::stream_base::timeout::suggested(beast::role_type::server));

  ws_.async_accept(req, beast::bind_front_handler(&WsSession::OnAccept,
                                                  shared_from_this()));
}

void WsSession::OnAccept(beast::error_code ec) {
  if (ec) {
    log::Warn("WebSocket 握手失败: {}", ec.message());
    return;
  }
  log::Info("WebSocket 连接建立");
  Send(json{{"type", "hello"},
            {"model", chat_.model_name()},
            {"voice", speech_ != nullptr},
            {"tts_sample_rate", speech_ ? speech_->tts_sample_rate() : 0}}
           .dump());
  DoRead();
}

void WsSession::DoRead() {
  ws_.async_read(buffer_, beast::bind_front_handler(&WsSession::OnRead,
                                                    shared_from_this()));
}

void WsSession::OnRead(beast::error_code ec, size_t /*bytes*/) {
  if (ec) {
    if (ec != websocket::error::closed) {
      log::Warn("WebSocket 读取失败: {}", ec.message());
    } else {
      log::Info("WebSocket 连接关闭");
    }
    OnDisconnect();
    return;
  }

  if (ws_.got_text()) {
    HandleText(beast::buffers_to_string(buffer_.data()));
  } else {
    const auto data = buffer_.data();
    HandleBinary(data.data(), data.size());
  }
  buffer_.consume(buffer_.size());
  DoRead();
}

void WsSession::OnDisconnect() {
  chat_.RequestStop();  // 单用户场景: 人走了就不用继续生成了
  if (speech_) {
    speech_->CancelTts();
    if (mic_acquired_) {
      speech_->ReleaseMic(this);
      speech_->SetCallbacks({});
      mic_acquired_ = false;
    }
  }
}

// ---------------------------------------------------------------------------
// 上行消息处理
// ---------------------------------------------------------------------------

void WsSession::HandleBinary(const void* data, size_t size) {
  if (size <= kFrameHeaderSize || !speech_ || !mic_acquired_) return;

  const auto* bytes = static_cast<const uint8_t*>(data);
  if (bytes[0] != kFrameMicAudio) return;

  const auto* pcm =
      reinterpret_cast<const int16_t*>(bytes + kFrameHeaderSize);
  const size_t count = (size - kFrameHeaderSize) / 2;
  std::vector<float> samples(count);
  for (size_t i = 0; i < count; ++i) {
    samples[i] = static_cast<float>(pcm[i]) / 32768.0f;
  }
  speech_->PushAudio(std::move(samples));
}

void WsSession::HandleText(const std::string& text) {
  const auto message = json::parse(text, /*cb=*/nullptr,
                                   /*allow_exceptions=*/false);
  const auto type = message.is_object() ? message.value("type", "") : "";

  if (type == "chat") {
    if (auto user_text = message.value("text", ""); !user_text.empty()) {
      StartChatTurn(std::move(user_text), /*speak=*/false);
    }
  } else if (type == "stop") {
    if (turn_cancelled_) turn_cancelled_->store(true);
    chat_.RequestStop();
    if (speech_) speech_->CancelTts();
    busy_ = false;
  } else if (type == "reset") {
    std::weak_ptr<WsSession> weak = shared_from_this();
    chat_.SubmitReset([weak](bool ok) {
      if (auto self = weak.lock()) {
        self->Send(ok ? json{{"type", "reset_done"}}.dump()
                      : json{{"type", "error"},
                             {"message", "清空会话失败"}}
                            .dump());
      }
    });
  } else if (type == "mic_start") {
    StartMic();
  } else if (type == "mic_stop") {
    StopMic();
  } else {
    log::Warn("未知的 WebSocket 消息类型: {}", type);
  }
}

// ---------------------------------------------------------------------------
// 语音编排
// ---------------------------------------------------------------------------

void WsSession::StartMic() {
  if (!speech_) {
    Send(json{{"type", "error"},
              {"message", "语音功能不可用(语音模型未加载)"}}
             .dump());
    return;
  }
  if (!speech_->TryAcquireMic(this)) {
    Send(json{{"type", "error"},
              {"message", "麦克风已被其他窗口占用"}}
             .dump());
    return;
  }
  mic_acquired_ = true;

  std::weak_ptr<WsSession> weak = shared_from_this();
  speech_->SetCallbacks({
      .on_speech_start =
          [weak] {
            if (auto self = weak.lock()) {
              self->Send(json{{"type", "speech_start"}}.dump());
            }
          },
      .on_utterance =
          [weak](engine::AsrResult result) {
            auto self = weak.lock();
            if (!self) return;
            if (self->busy_) {
              // M4 半双工: 回答期间的语音先忽略(M5 做打断)
              log::Info("回答进行中, 忽略语音: {}", result.text);
              return;
            }
            self->Send(json{{"type", "asr_text"},
                            {"text", result.text},
                            {"lang", result.lang}}
                           .dump());
            self->StartChatTurn(std::move(result.text), /*speak=*/true);
          },
      .on_tts_audio =
          [weak](AudioBuffer audio, std::string /*sentence*/) {
            if (auto self = weak.lock()) {
              self->Send(EncodeTtsFrame(audio), /*binary=*/true);
            }
          },
      .on_tts_done =
          [weak] {
            if (auto self = weak.lock()) {
              self->busy_ = false;
              self->Send(json{{"type", "tts_end"}}.dump());
            }
          },
      .on_error =
          [weak](std::string error) {
            if (auto self = weak.lock()) {
              self->Send(json{{"type", "error"},
                              {"message", std::move(error)}}
                             .dump());
            }
          },
  });
  Send(json{{"type", "mic_ready"}}.dump());
}

void WsSession::StopMic() {
  if (speech_ && mic_acquired_) speech_->FlushMic();
  // 保留占用与回调: 尾段识别结果仍要走完问答流程
}

// ---------------------------------------------------------------------------
// 一轮对话(文字/语音共用)
// ---------------------------------------------------------------------------

void WsSession::StartChatTurn(std::string user_text, bool speak) {
  busy_ = true;
  auto cancelled = std::make_shared<std::atomic<bool>>(false);
  turn_cancelled_ = cancelled;
  // 每轮一个独立切句器, 由 LLM 线程独占使用
  auto splitter = std::make_shared<dialog::SentenceSplitter>();

  std::weak_ptr<WsSession> weak = shared_from_this();
  ChatService::Callbacks callbacks{
      .on_delta =
          [weak, splitter, speak, cancelled](std::string piece) {
            auto self = weak.lock();
            if (!self) return;
            if (speak && !cancelled->load()) {
              for (auto& sentence : splitter->Feed(piece)) {
                self->speech_->SubmitSentence(std::move(sentence));
              }
            }
            self->Send(json{{"type", "llm_delta"},
                            {"text", std::move(piece)}}
                           .dump());
          },
      .on_done =
          [weak, splitter, speak, cancelled](engine::LlmTurnStats stats) {
            auto self = weak.lock();
            if (!self) return;
            if (speak) {
              if (!cancelled->load()) {
                if (auto tail = splitter->Flush(); !tail.empty()) {
                  self->speech_->SubmitSentence(std::move(tail));
                }
              }
              // 哨兵: 此前句子全部合成完毕后触发 tts_end + busy 复位
              self->speech_->SubmitTtsDoneMarker();
            } else {
              self->busy_ = false;
            }
            self->Send(json{{"type", "llm_end"},
                            {"prompt_tokens", stats.prompt_tokens},
                            {"new_tokens", stats.new_tokens},
                            {"first_token_seconds", stats.first_token_seconds},
                            {"tokens_per_second", stats.TokensPerSecond()}}
                           .dump());
          },
      .on_error =
          [weak](std::string error) {
            if (auto self = weak.lock()) {
              self->Send(json{{"type", "error"},
                              {"message", std::move(error)}}
                             .dump());
            }
          },
  };
  chat_.SubmitChat(std::move(user_text), std::move(callbacks));
}

// ---------------------------------------------------------------------------
// 发送(线程安全入口 + io 线程写队列)
// ---------------------------------------------------------------------------

void WsSession::Send(std::string payload, bool binary) {
  boost::asio::post(ws_.get_executor(),
                    [self = shared_from_this(), p = std::move(payload),
                     binary]() mutable {
                      self->QueueWrite(std::move(p), binary);
                    });
}

void WsSession::QueueWrite(std::string payload, bool binary) {
  outbox_.push_back({std::move(payload), binary});
  if (outbox_.size() == 1) DoWrite();  // 无在途写操作时启动写链
}

void WsSession::DoWrite() {
  const auto& next = outbox_.front();
  ws_.binary(next.binary);
  ws_.async_write(boost::asio::buffer(next.data),
                  beast::bind_front_handler(&WsSession::OnWrite,
                                            shared_from_this()));
}

void WsSession::OnWrite(beast::error_code ec, size_t /*bytes*/) {
  if (ec) {
    log::Warn("WebSocket 发送失败: {}", ec.message());
    return;
  }
  outbox_.pop_front();
  if (!outbox_.empty()) DoWrite();
}

}  // namespace echo::server
