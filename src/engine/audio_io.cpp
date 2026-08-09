#include "engine/audio_io.h"

#include "sherpa-onnx/c-api/cxx-api.h"

namespace echo::engine {

Result<AudioBuffer> ReadWavFile(const std::filesystem::path& file) {
  if (!std::filesystem::exists(file)) {
    return Fail(ErrorCode::kIo, "音频文件不存在: {}", file.string());
  }
  auto wave = sherpa_onnx::cxx::ReadWave(file.string());
  if (wave.samples.empty() || wave.sample_rate <= 0) {
    return Fail(ErrorCode::kIo, "读取 wav 失败(需要 16-bit PCM 格式): {}",
                file.string());
  }
  return AudioBuffer{std::move(wave.samples), wave.sample_rate};
}

Result<void> WriteWavFile(const std::filesystem::path& file,
                          const AudioBuffer& audio) {
  sherpa_onnx::cxx::Wave wave;
  wave.samples = audio.samples;
  wave.sample_rate = audio.sample_rate;
  if (!sherpa_onnx::cxx::WriteWave(file.string(), wave)) {
    return Fail(ErrorCode::kIo, "写出 wav 失败: {}", file.string());
  }
  return {};
}

}  // namespace echo::engine
