#include "engine/vad/silero_vad.h"

#include <filesystem>

#include "sherpa-onnx/c-api/cxx-api.h"

namespace echo::engine {

struct SileroVad::Impl {
  sherpa_onnx::cxx::VoiceActivityDetector vad;
  int window_size = 512;       // silero v4/v5 在 16kHz 下固定 512
  std::vector<float> pending;  // 不足一个窗口的余量

  // sherpa 包装类没有默认构造函数, Impl 只能带着现成对象构造
  explicit Impl(sherpa_onnx::cxx::VoiceActivityDetector v)
      : vad(std::move(v)) {}
};

SileroVad::SileroVad() = default;
SileroVad::~SileroVad() = default;
SileroVad::SileroVad(SileroVad&&) noexcept = default;
SileroVad& SileroVad::operator=(SileroVad&&) noexcept = default;

Result<std::unique_ptr<SileroVad>> SileroVad::Create(const VadConfig& config) {
  if (!std::filesystem::exists(config.model)) {
    return Fail(ErrorCode::kModelLoadFailed, "VAD 模型文件不存在: {}",
                config.model.string());
  }

  sherpa_onnx::cxx::VadModelConfig sherpa_config;
  auto& silero = sherpa_config.silero_vad;
  silero.model = config.model.string();
  silero.threshold = config.threshold;
  silero.min_silence_duration = config.min_silence_seconds;
  silero.min_speech_duration = config.min_speech_seconds;
  silero.max_speech_duration = config.max_speech_seconds;
  sherpa_config.sample_rate = config.sample_rate;

  // 缓冲区容量取"最长语音段 + 静音余量"
  const float buffer_seconds = config.max_speech_seconds + 10.0f;
  auto vad = sherpa_onnx::cxx::VoiceActivityDetector::Create(sherpa_config,
                                                             buffer_seconds);
  if (vad.Get() == nullptr) {
    return Fail(ErrorCode::kModelLoadFailed, "创建 VAD 失败: {}",
                config.model.string());
  }

  auto engine = std::unique_ptr<SileroVad>(new SileroVad());
  engine->impl_ = std::make_unique<Impl>(std::move(vad));
  return engine;
}

void SileroVad::Feed(std::span<const float> samples) {
  auto& impl = *impl_;
  impl.pending.insert(impl.pending.end(), samples.begin(), samples.end());

  size_t offset = 0;
  const auto window = static_cast<size_t>(impl.window_size);
  while (impl.pending.size() - offset >= window) {
    impl.vad.AcceptWaveform(impl.pending.data() + offset,
                            impl.window_size);
    offset += window;
  }
  impl.pending.erase(impl.pending.begin(),
                     impl.pending.begin() + static_cast<ptrdiff_t>(offset));
}

std::optional<SpeechSegment> SileroVad::PopSegment() {
  auto& vad = impl_->vad;
  if (vad.IsEmpty()) return std::nullopt;

  auto segment = vad.Front();
  vad.Pop();
  return SpeechSegment{segment.start, std::move(segment.samples)};
}

bool SileroVad::IsSpeaking() const { return impl_->vad.IsDetected(); }

void SileroVad::Flush() {
  auto& impl = *impl_;
  if (!impl.pending.empty()) {
    // 补零凑满一个窗口, 让尾部语音也能被处理
    impl.pending.resize(static_cast<size_t>(impl.window_size), 0.0f);
    impl.vad.AcceptWaveform(impl.pending.data(), impl.window_size);
    impl.pending.clear();
  }
  impl.vad.Flush();
}

void SileroVad::Reset() {
  impl_->vad.Reset();
  impl_->pending.clear();
}

}  // namespace echo::engine
