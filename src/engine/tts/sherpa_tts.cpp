#include "engine/tts/sherpa_tts.h"

#include <filesystem>

#include "sherpa-onnx/c-api/cxx-api.h"

namespace echo::engine {

struct SherpaTts::Impl {
  sherpa_onnx::cxx::OfflineTts tts;
  TtsConfig config;

  // sherpa 包装类没有默认构造函数, Impl 只能带着现成对象构造
  Impl(sherpa_onnx::cxx::OfflineTts t, TtsConfig c)
      : tts(std::move(t)), config(std::move(c)) {}
};

SherpaTts::SherpaTts() = default;
SherpaTts::~SherpaTts() = default;
SherpaTts::SherpaTts(SherpaTts&&) noexcept = default;
SherpaTts& SherpaTts::operator=(SherpaTts&&) noexcept = default;

Result<std::unique_ptr<SherpaTts>> SherpaTts::Create(const TtsConfig& config) {
  for (const auto& file : {config.model, config.lexicon, config.tokens}) {
    if (!std::filesystem::exists(file)) {
      return Fail(ErrorCode::kModelLoadFailed, "TTS 模型文件不存在: {}",
                  file.string());
    }
  }

  sherpa_onnx::cxx::OfflineTtsConfig sherpa_config;
  auto& vits = sherpa_config.model.vits;
  vits.model = config.model.string();
  vits.lexicon = config.lexicon.string();
  vits.tokens = config.tokens.string();
  // 新版 sherpa-onnx 中 dict_dir 为兼容保留字段, C++ 包装层不再透传,
  // 中文分词词典已内置处理; 保留配置项以兼容旧版模型包
  vits.dict_dir = config.dict_dir.string();
  sherpa_config.model.num_threads = config.num_threads;

  auto tts = sherpa_onnx::cxx::OfflineTts::Create(sherpa_config);
  if (tts.Get() == nullptr) {
    return Fail(ErrorCode::kModelLoadFailed,
                "创建 TTS 引擎失败(检查模型文件是否完整): {}",
                config.model.string());
  }

  auto engine = std::unique_ptr<SherpaTts>(new SherpaTts());
  engine->impl_ = std::make_unique<Impl>(std::move(tts), config);
  return engine;
}

Result<AudioBuffer> SherpaTts::Synthesize(const std::string& text) const {
  if (text.empty()) {
    return Fail(ErrorCode::kGenerationFailed, "合成文本为空");
  }

  sherpa_onnx::cxx::GenerationConfig generation;
  generation.sid = impl_->config.speaker_id;
  generation.speed = impl_->config.speed;

  auto audio = impl_->tts.Generate(text, generation);
  if (audio.samples.empty() || audio.sample_rate <= 0) {
    return Fail(ErrorCode::kGenerationFailed, "合成失败(输出为空): {}",
                text.substr(0, 32));
  }
  return AudioBuffer{std::move(audio.samples), audio.sample_rate};
}

int SherpaTts::SampleRate() const { return impl_->tts.SampleRate(); }

}  // namespace echo::engine
