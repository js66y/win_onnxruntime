#pragma once

#include <memory>
#include <string>

#include "common/audio.h"
#include "common/config.h"
#include "common/error.h"

namespace echo::engine {

// vits(melo-tts)离线语音合成的 RAII 封装(pimpl)。
class SherpaTts {
 public:
  [[nodiscard]] static Result<std::unique_ptr<SherpaTts>> Create(
      const TtsConfig& config);

  ~SherpaTts();
  SherpaTts(SherpaTts&&) noexcept;
  SherpaTts& operator=(SherpaTts&&) noexcept;
  SherpaTts(const SherpaTts&) = delete;
  SherpaTts& operator=(const SherpaTts&) = delete;

  // 合成一段文本(说话人与语速取自配置)
  [[nodiscard]] Result<AudioBuffer> Synthesize(const std::string& text) const;

  [[nodiscard]] int SampleRate() const;

 private:
  SherpaTts();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace echo::engine
