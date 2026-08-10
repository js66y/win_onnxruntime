#include "server/speech_service.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <variant>

#include "common/bounded_queue.h"
#include "common/log.h"
#include "dialog/sentence_splitter.h"
#include "engine/tts/sherpa_tts.h"
#include "engine/vad/silero_vad.h"

namespace echo::server {

namespace {

struct FlushMicMarker {};
using AudioTask = std::variant<std::vector<float>, FlushMicMarker>;

struct TtsDoneMarker {};
using TtsTask = std::variant<std::string, TtsDoneMarker>;

}  // namespace

struct SpeechService::Impl {
  std::unique_ptr<engine::SileroVad> vad;
  std::unique_ptr<engine::SherpaAsr> asr;
  std::unique_ptr<engine::SherpaTts> tts;
  int tts_sample_rate = 0;

  std::mutex callbacks_mutex;
  Callbacks callbacks;

  std::atomic<const void*> mic_owner{nullptr};
  std::atomic<uint64_t> tts_epoch{0};  // CancelTts 时自增, 丢弃过期合成结果
  bool was_speaking = false;  // 仅音频线程访问

  BoundedQueue<AudioTask> audio_queue{256};
  BoundedQueue<TtsTask> tts_queue{64};

  // 声明在队列之后: 析构时线程先停, 队列后销毁
  std::jthread audio_worker;
  std::jthread tts_worker;

  Callbacks GetCallbacks() {
    std::scoped_lock lock(callbacks_mutex);
    return callbacks;
  }

  // ---------- 音频线程: VAD -> ASR ----------
  void AudioLoop(std::stop_token stop) {
    while (auto task = audio_queue.Pop(stop)) {
      if (std::holds_alternative<FlushMicMarker>(*task)) {
        vad->Flush();
      } else {
        vad->Feed(std::get<std::vector<float>>(*task));
      }
      DrainVad();
    }
  }

  void DrainVad() {
    const auto handlers = GetCallbacks();

    // "开始说话"边沿检测
    const bool speaking = vad->IsSpeaking();
    if (speaking && !was_speaking && handlers.on_speech_start) {
      handlers.on_speech_start();
    }
    was_speaking = speaking;

    while (auto segment = vad->PopSegment()) {
      auto result = asr->Recognize(segment->samples, 16000);
      if (!result) {
        if (handlers.on_error) handlers.on_error(result.error().message);
        continue;
      }
      if (result->text.empty()) continue;  // 静噪误触发
      log::Info("识别: {}", result->text);
      if (handlers.on_utterance) handlers.on_utterance(*std::move(result));
    }
  }

  // ---------- 合成线程: 句子 -> PCM ----------
  void TtsLoop(std::stop_token stop) {
    while (auto task = tts_queue.Pop(stop)) {
      const auto handlers = GetCallbacks();
      const uint64_t epoch_at_start = tts_epoch.load();

      if (std::holds_alternative<TtsDoneMarker>(*task)) {
        // 打断后入队的旧哨兵直接丢弃; 新轮次会再放自己的哨兵
        if (tts_epoch.load() != epoch_at_start) continue;
        if (handlers.on_tts_done) handlers.on_tts_done();
        continue;
      }

      auto& sentence = std::get<std::string>(*task);
      const auto speakable = dialog::StripForTts(sentence);
      if (speakable.find_first_not_of(" \t\r\n") == std::string::npos) {
        continue;
      }

      auto audio = tts->Synthesize(speakable);
      if (tts_epoch.load() != epoch_at_start) continue;  // 合成期间被打断
      if (!audio) {
        if (handlers.on_error) handlers.on_error(audio.error().message);
        continue;
      }
      if (handlers.on_tts_audio) {
        handlers.on_tts_audio(*std::move(audio), std::move(sentence));
      }
    }
  }
};

SpeechService::SpeechService() : impl_(std::make_unique<Impl>()) {}

SpeechService::~SpeechService() {
  if (!impl_) return;
  impl_->audio_worker.request_stop();
  impl_->tts_worker.request_stop();
}

Result<std::unique_ptr<SpeechService>> SpeechService::Create(
    const AppConfig& config) {
  auto vad = engine::SileroVad::Create(config.vad);
  if (!vad) return std::unexpected(vad.error());
  auto asr = engine::SherpaAsr::Create(config.asr);
  if (!asr) return std::unexpected(asr.error());
  auto tts = engine::SherpaTts::Create(config.tts);
  if (!tts) return std::unexpected(tts.error());

  auto service = std::unique_ptr<SpeechService>(new SpeechService());
  auto& impl = *service->impl_;
  impl.vad = std::move(*vad);
  impl.asr = std::move(*asr);
  impl.tts = std::move(*tts);
  impl.tts_sample_rate = impl.tts->SampleRate();
  impl.audio_worker =
      std::jthread([&impl](std::stop_token stop) { impl.AudioLoop(stop); });
  impl.tts_worker =
      std::jthread([&impl](std::stop_token stop) { impl.TtsLoop(stop); });
  return service;
}

bool SpeechService::TryAcquireMic(const void* owner) {
  const void* expected = nullptr;
  return impl_->mic_owner.compare_exchange_strong(expected, owner) ||
         expected == owner;
}

void SpeechService::ReleaseMic(const void* owner) {
  const void* expected = owner;
  if (impl_->mic_owner.compare_exchange_strong(expected, nullptr)) {
    // 丢弃残留音频, 复位 VAD, 下一个用户从干净状态开始
    impl_->audio_queue.Clear();
    impl_->vad->Reset();
  }
}

void SpeechService::SetCallbacks(Callbacks callbacks) {
  std::scoped_lock lock(impl_->callbacks_mutex);
  impl_->callbacks = std::move(callbacks);
}

void SpeechService::PushAudio(std::vector<float> samples) {
  // 非阻塞: ASR 偶发积压时宁可丢帧, 也绝不阻塞网络 IO 线程
  if (!impl_->audio_queue.TryPush(std::move(samples))) {
    log::Warn("音频队列已满, 丢弃一帧");
  }
}

void SpeechService::FlushMic() {
  impl_->audio_queue.Push(FlushMicMarker{},
                          impl_->audio_worker.get_stop_token());
}

void SpeechService::SubmitSentence(std::string sentence) {
  impl_->tts_queue.Push(std::move(sentence),
                        impl_->tts_worker.get_stop_token());
}

void SpeechService::SubmitTtsDoneMarker() {
  impl_->tts_queue.Push(TtsDoneMarker{}, impl_->tts_worker.get_stop_token());
}

void SpeechService::CancelTts() {
  impl_->tts_epoch.fetch_add(1);
  impl_->tts_queue.Clear();
}

int SpeechService::tts_sample_rate() const { return impl_->tts_sample_rate; }

}  // namespace echo::server
