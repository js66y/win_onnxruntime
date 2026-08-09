#pragma once

#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "common/config.h"
#include "common/error.h"

namespace echo::engine {

// 一段被 VAD 切出的完整语音
struct SpeechSegment {
  int start_sample = 0;        // 相对整条音频流的起始采样点
  std::vector<float> samples;  // 16kHz 单声道
};

// silero VAD 的 RAII 封装(pimpl)。
// 用法: 持续 Feed() 音频帧 -> 循环 PopSegment() 取完整语音段;
// 输入结束时调用 Flush() 逼出最后一段。
class SileroVad {
 public:
  [[nodiscard]] static Result<std::unique_ptr<SileroVad>> Create(
      const VadConfig& config);

  ~SileroVad();
  SileroVad(SileroVad&&) noexcept;
  SileroVad& operator=(SileroVad&&) noexcept;
  SileroVad(const SileroVad&) = delete;
  SileroVad& operator=(const SileroVad&) = delete;

  // 送入任意长度的音频(内部按模型窗口大小切块)
  void Feed(std::span<const float> samples);

  // 取出一段已完成的语音段, 没有则返回 nullopt
  [[nodiscard]] std::optional<SpeechSegment> PopSegment();

  // 当前是否正处于"有人说话"状态(实时打断判断用)
  [[nodiscard]] bool IsSpeaking() const;

  // 输入结束, 冲出缓冲中的最后一段语音
  void Flush();

  // 复位内部状态
  void Reset();

 private:
  SileroVad();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace echo::engine
