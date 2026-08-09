#pragma once

#include <memory>
#include <span>
#include <string>

#include "common/config.h"
#include "common/error.h"

namespace echo::engine {

struct AsrResult {
  std::string text;
  std::string lang;     // 如 "zh" / "en", 模型未输出时为空
  std::string emotion;  // 如 "NEUTRAL" / "HAPPY", 模型未输出时为空
};

// SenseVoice 离线语音识别的 RAII 封装(pimpl 隐藏 sherpa-onnx 头文件)。
// 任意采样率输入均可, 内部特征提取会自动重采样。
class SherpaAsr {
 public:
  [[nodiscard]] static Result<std::unique_ptr<SherpaAsr>> Create(
      const AsrConfig& config);

  ~SherpaAsr();
  SherpaAsr(SherpaAsr&&) noexcept;
  SherpaAsr& operator=(SherpaAsr&&) noexcept;
  SherpaAsr(const SherpaAsr&) = delete;
  SherpaAsr& operator=(const SherpaAsr&) = delete;

  [[nodiscard]] Result<AsrResult> Recognize(std::span<const float> samples,
                                            int sample_rate) const;

 private:
  SherpaAsr();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace echo::engine
