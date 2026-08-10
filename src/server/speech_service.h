#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "common/audio.h"
#include "common/config.h"
#include "common/error.h"
#include "engine/asr/sherpa_asr.h"

namespace echo::server {

// 语音服务: 拥有 VAD/ASR/TTS 三个引擎与两条工作线程。
//
//   音频线程:  PushAudio(16kHz PCM) -> VAD 切段 -> ASR 识别 -> on_utterance
//   合成线程:  SubmitSentence(句子) -> TTS 合成 -> on_tts_audio
//
// 所有回调都在内部线程触发, 调用方负责切回自己的执行器。
// 同一时间只允许一个会话占用麦克风(TryAcquireMic)。
class SpeechService {
 public:
  struct Callbacks {
    // VAD 检测到开始说话(前端做视觉反馈)
    std::function<void()> on_speech_start;
    // 一段语音识别完成
    std::function<void(engine::AsrResult result)> on_utterance;
    // 一句话合成完成
    std::function<void(AudioBuffer audio, std::string sentence)> on_tts_audio;
    // 此前提交的所有句子都已合成完毕(见 SubmitTtsDoneMarker)
    std::function<void()> on_tts_done;
    std::function<void(std::string message)> on_error;
  };

  [[nodiscard]] static Result<std::unique_ptr<SpeechService>> Create(
      const AppConfig& config);

  ~SpeechService();
  SpeechService(const SpeechService&) = delete;
  SpeechService& operator=(const SpeechService&) = delete;

  // 麦克风占用权: owner 用连接指针即可。失败返回 false(已被占用)。
  [[nodiscard]] bool TryAcquireMic(const void* owner);
  void ReleaseMic(const void* owner);

  // 注册回调(仅麦克风持有者调用; 简化: 全局一份)
  void SetCallbacks(Callbacks callbacks);

  // 送入 16kHz 单声道样本(来自 WebSocket 二进制帧)
  void PushAudio(std::vector<float> samples);

  // 麦克风关闭: 冲出 VAD 缓冲中的尾段
  void FlushMic();

  // 提交一句待合成文本(排队)
  void SubmitSentence(std::string sentence);

  // 在当前批次句子之后放一个"完成"哨兵, 全部合成完时触发 on_tts_done
  void SubmitTtsDoneMarker();

  // 丢弃所有排队中的合成任务(打断)
  void CancelTts();

  [[nodiscard]] int tts_sample_rate() const;

 private:
  SpeechService();

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace echo::server
