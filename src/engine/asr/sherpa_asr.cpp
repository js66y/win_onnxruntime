#include "engine/asr/sherpa_asr.h"

#include <algorithm>
#include <filesystem>

#include "sherpa-onnx/c-api/cxx-api.h"

namespace echo::engine {

namespace {

// SenseVoice 输出的语言/情感形如 "<|zh|>", 剥掉包装只留内容
std::string StripTag(const std::string& tag) {
  std::string out;
  std::ranges::copy_if(tag, std::back_inserter(out),
                       [](char c) { return c != '<' && c != '>' && c != '|'; });
  return out;
}

}  // namespace

struct SherpaAsr::Impl {
  sherpa_onnx::cxx::OfflineRecognizer recognizer;

  // sherpa 包装类没有默认构造函数, Impl 只能带着现成对象构造
  explicit Impl(sherpa_onnx::cxx::OfflineRecognizer r)
      : recognizer(std::move(r)) {}
};

SherpaAsr::SherpaAsr() = default;
SherpaAsr::~SherpaAsr() = default;
SherpaAsr::SherpaAsr(SherpaAsr&&) noexcept = default;
SherpaAsr& SherpaAsr::operator=(SherpaAsr&&) noexcept = default;

Result<std::unique_ptr<SherpaAsr>> SherpaAsr::Create(const AsrConfig& config) {
  for (const auto& file : {config.model, config.tokens}) {
    if (!std::filesystem::exists(file)) {
      return Fail(ErrorCode::kModelLoadFailed, "ASR 模型文件不存在: {}",
                  file.string());
    }
  }

  sherpa_onnx::cxx::OfflineRecognizerConfig sherpa_config;
  sherpa_config.model_config.sense_voice.model = config.model.string();
  sherpa_config.model_config.sense_voice.language = config.language;
  sherpa_config.model_config.sense_voice.use_itn = config.use_itn;
  sherpa_config.model_config.tokens = config.tokens.string();
  sherpa_config.model_config.num_threads = config.num_threads;

  auto recognizer =
      sherpa_onnx::cxx::OfflineRecognizer::Create(sherpa_config);
  if (recognizer.Get() == nullptr) {
    return Fail(ErrorCode::kModelLoadFailed,
                "创建 SenseVoice 识别器失败(检查模型文件是否完整): {}",
                config.model.string());
  }

  auto asr = std::unique_ptr<SherpaAsr>(new SherpaAsr());
  asr->impl_ = std::make_unique<Impl>(std::move(recognizer));
  return asr;
}

Result<AsrResult> SherpaAsr::Recognize(std::span<const float> samples,
                                       int sample_rate) const {
  if (samples.empty() || sample_rate <= 0) {
    return Fail(ErrorCode::kGenerationFailed, "识别输入为空");
  }

  auto stream = impl_->recognizer.CreateStream();
  if (stream.Get() == nullptr) {
    return Fail(ErrorCode::kGenerationFailed, "创建识别流失败");
  }
  stream.AcceptWaveform(sample_rate, samples.data(),
                        static_cast<int32_t>(samples.size()));
  impl_->recognizer.Decode(&stream);
  auto result = impl_->recognizer.GetResult(&stream);

  return AsrResult{
      .text = std::move(result.text),
      .lang = StripTag(result.lang),
      .emotion = StripTag(result.emotion),
  };
}

}  // namespace echo::engine
