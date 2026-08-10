#include "server/ws_session.h"

#include <boost/asio/post.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

#include <nlohmann/json.hpp>

#include "common/log.h"
#include "dialog/sentence_splitter.h"

namespace echo::server {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
using json = nlohmann::json;

namespace {

constexpr uint8_t kFrameMicAudio = 0x01;
constexpr uint8_t kFrameTtsAudio = 0x02;
constexpr size_t kFrameHeaderSize = 4;

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

json RolesJson(const AppConfig& config) {
  json arr = json::array();
  for (const auto& role : config.roles) {
    arr.push_back({{"id", role.id}, {"name", role.name}});
  }
  return arr;
}

}  // namespace

WsSession::WsSession(beast::tcp_stream stream, ServerContext& ctx)
    : ws_(std::move(stream)), ctx_(ctx) {
  role_id_ = ctx_.config->active_role;
}

WsSession::~WsSession() {
  if (mic_acquired_ && ctx_.speech) ctx_.speech->ReleaseMic(this);
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

  if (auto created = ctx_.store->CreateSession(role_id_)) {
    session_id_ = created->id;
  } else {
    log::Error("创建会话失败: {}", created.error().message);
  }

  Send(json{{"type", "hello"},
            {"model", ctx_.chat->model_name()},
            {"voice", ctx_.speech != nullptr},
            {"tts_sample_rate",
             ctx_.speech ? ctx_.speech->tts_sample_rate() : 0},
            {"session_id", session_id_},
            {"role", role_id_},
            {"roles", RolesJson(*ctx_.config)},
            {"barge_in", true}}
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
  InterruptCurrentTurn();
  if (ctx_.speech && mic_acquired_) {
    ctx_.speech->ReleaseMic(this);
    ctx_.speech->SetCallbacks({});
    mic_acquired_ = false;
  }
}

void WsSession::HandleBinary(const void* data, size_t size) {
  if (size <= kFrameHeaderSize || !ctx_.speech || !mic_acquired_) return;
  const auto* bytes = static_cast<const uint8_t*>(data);
  if (bytes[0] != kFrameMicAudio) return;

  const auto* pcm =
      reinterpret_cast<const int16_t*>(bytes + kFrameHeaderSize);
  const size_t count = (size - kFrameHeaderSize) / 2;
  std::vector<float> samples(count);
  for (size_t i = 0; i < count; ++i) {
    samples[i] = static_cast<float>(pcm[i]) / 32768.0f;
  }
  ctx_.speech->PushAudio(std::move(samples));
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
    InterruptCurrentTurn();
  } else if (type == "reset" || type == "new_session") {
    HandleNewSession();
  } else if (type == "set_role") {
    HandleSetRole(message.value("id", ""));
  } else if (type == "get_history") {
    if (auto messages = ctx_.store->ListMessages(session_id_)) {
      json arr = json::array();
      for (const auto& m : *messages) {
        arr.push_back({{"role", m.role}, {"content", m.content}});
      }
      Send(json{{"type", "history"}, {"messages", std::move(arr)}}.dump());
    }
  } else if (type == "mic_start") {
    StartMic();
  } else if (type == "mic_stop") {
    StopMic();
  } else {
    log::Warn("未知的 WebSocket 消息类型: {}", type);
  }
}

void WsSession::InterruptCurrentTurn() {
  if (turn_cancelled_) turn_cancelled_->store(true);
  ctx_.chat->RequestStop();
  if (ctx_.speech) ctx_.speech->CancelTts();
  if (busy_.exchange(false)) {
    Send(json{{"type", "interrupt"}}.dump());
  }
}

void WsSession::StartMic() {
  if (!ctx_.speech) {
    Send(json{{"type", "error"},
              {"message", "语音功能不可用(语音模型未加载)"}}
             .dump());
    return;
  }
  if (!ctx_.speech->TryAcquireMic(this)) {
    Send(json{{"type", "error"},
              {"message", "麦克风已被其他窗口占用"}}
             .dump());
    return;
  }
  mic_acquired_ = true;

  std::weak_ptr<WsSession> weak = shared_from_this();
  ctx_.speech->SetCallbacks({
      .on_speech_start =
          [weak] {
            auto self = weak.lock();
            if (!self) return;
            // 全双工打断: 播报/生成中听到人声立刻停
            if (self->busy_) self->InterruptCurrentTurn();
            self->Send(json{{"type", "speech_start"}}.dump());
          },
      .on_utterance =
          [weak](engine::AsrResult result) {
            auto self = weak.lock();
            if (!self) return;
            if (self->busy_) self->InterruptCurrentTurn();
            self->Send(json{{"type", "asr_text"},
                            {"text", result.text},
                            {"lang", result.lang}}
                           .dump());
            self->StartChatTurn(std::move(result.text), /*speak=*/true);
          },
      .on_tts_audio =
          [weak](AudioBuffer audio, std::string /*sentence*/) {
            auto self = weak.lock();
            if (!self) return;
            if (self->turn_cancelled_ && self->turn_cancelled_->load()) return;
            self->Send(EncodeTtsFrame(audio), /*binary=*/true);
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
  if (ctx_.speech && mic_acquired_) ctx_.speech->FlushMic();
}

void WsSession::Persist(std::string role, std::string content) {
  if (session_id_.empty() || content.empty()) return;
  if (auto ok = ctx_.store->AppendMessage(session_id_, std::move(role),
                                          std::move(content));
      !ok) {
    log::Warn("持久化失败: {}", ok.error().message);
  }
}

void WsSession::ReplyDirect(std::string text, bool speak,
                            std::string tool_name) {
  busy_ = true;
  auto cancelled = std::make_shared<std::atomic<bool>>(false);
  turn_cancelled_ = cancelled;

  Persist("assistant", text);
  Send(json{{"type", "tool_result"},
            {"tool", tool_name},
            {"text", text}}
           .dump());
  Send(json{{"type", "llm_delta"}, {"text", text}}.dump());
  Send(json{{"type", "llm_end"},
            {"prompt_tokens", 0},
            {"new_tokens", 0},
            {"first_token_seconds", 0.0},
            {"tokens_per_second", 0.0}}
           .dump());

  if (speak && ctx_.speech) {
    ctx_.speech->SubmitSentence(text);
    ctx_.speech->SubmitTtsDoneMarker();
  } else {
    busy_ = false;
  }
}

void WsSession::StartChatTurn(std::string user_text, bool speak) {
  if (busy_) InterruptCurrentTurn();

  Persist("user", user_text);

  // 本地工具优先(时间/计算/系统信息), 不经 LLM
  if (auto tool = ctx_.tools.TryHandle(user_text)) {
    log::Info("工具命中: {} -> {}", tool->tool_name, tool->display);
    ReplyDirect(std::move(tool->display), speak, tool->tool_name);
    return;
  }

  busy_ = true;
  auto cancelled = std::make_shared<std::atomic<bool>>(false);
  turn_cancelled_ = cancelled;
  auto splitter = std::make_shared<dialog::SentenceSplitter>();
  auto reply = std::make_shared<std::string>();
  reply_buffer_ = reply;

  std::weak_ptr<WsSession> weak = shared_from_this();
  ChatService::Callbacks callbacks{
      .on_delta =
          [weak, splitter, speak, cancelled, reply](std::string piece) {
            auto self = weak.lock();
            if (!self) return;
            *reply += piece;
            if (speak && self->ctx_.speech && !cancelled->load()) {
              for (auto& sentence : splitter->Feed(piece)) {
                self->ctx_.speech->SubmitSentence(std::move(sentence));
              }
            }
            self->Send(json{{"type", "llm_delta"},
                            {"text", std::move(piece)}}
                           .dump());
          },
      .on_done =
          [weak, splitter, speak, cancelled, reply](
              engine::LlmTurnStats stats) {
            auto self = weak.lock();
            if (!self) return;
            if (!reply->empty()) self->Persist("assistant", *reply);

            if (speak && self->ctx_.speech) {
              if (!cancelled->load()) {
                if (auto tail = splitter->Flush(); !tail.empty()) {
                  self->ctx_.speech->SubmitSentence(std::move(tail));
                }
              }
              self->ctx_.speech->SubmitTtsDoneMarker();
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
              self->busy_ = false;
              self->Send(json{{"type", "error"},
                              {"message", std::move(error)}}
                             .dump());
            }
          },
  };
  ctx_.chat->SubmitChat(std::move(user_text), std::move(callbacks));
}

void WsSession::HandleSetRole(const std::string& role_id) {
  const auto* role = ctx_.config->FindRole(role_id);
  if (!role) {
    Send(json{{"type", "error"}, {"message", "未知角色"}}.dump());
    return;
  }
  InterruptCurrentTurn();
  role_id_ = role->id;

  // 工具提示拼进 system prompt
  auto prompt = role->system_prompt + "\n" + ctx_.tools.PromptHint();
  std::weak_ptr<WsSession> weak = shared_from_this();
  ctx_.chat->SubmitRestart(std::move(prompt), [weak, id = role->id,
                                               name = role->name](bool ok) {
    if (auto self = weak.lock()) {
      if (ok) {
        self->ctx_.store->SetSessionRole(self->session_id_, id);
        self->Send(json{{"type", "role_changed"},
                        {"id", id},
                        {"name", name}}
                       .dump());
      } else {
        self->Send(json{{"type", "error"},
                        {"message", "切换角色失败"}}
                       .dump());
      }
    }
  });
}

void WsSession::HandleNewSession() {
  InterruptCurrentTurn();
  std::weak_ptr<WsSession> weak = shared_from_this();
  ctx_.chat->SubmitReset([weak](bool ok) {
    auto self = weak.lock();
    if (!self) return;
    if (!ok) {
      self->Send(json{{"type", "error"},
                      {"message", "清空会话失败"}}
                     .dump());
      return;
    }
    if (auto created = self->ctx_.store->CreateSession(self->role_id_)) {
      self->session_id_ = created->id;
    }
    self->Send(json{{"type", "reset_done"},
                    {"session_id", self->session_id_}}
                   .dump());
  });
}

void WsSession::Send(std::string payload, bool binary) {
  boost::asio::post(ws_.get_executor(),
                    [self = shared_from_this(), p = std::move(payload),
                     binary]() mutable {
                      self->QueueWrite(std::move(p), binary);
                    });
}

void WsSession::QueueWrite(std::string payload, bool binary) {
  outbox_.push_back({std::move(payload), binary});
  if (outbox_.size() == 1) DoWrite();
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
