#pragma once

#include <filesystem>
#include <string>

#include "common/error.h"

namespace echo {

struct LlmConfig {
  std::filesystem::path model_dir;
  int max_length = 4096;   // 上下文总 token 上限(含提示词)
  bool do_sample = true;
  float temperature = 0.7f;
  float top_p = 0.8f;
  bool disable_thinking = true;  // 关闭 Qwen3 思考模式
};

// SenseVoice 离线识别
struct AsrConfig {
  std::filesystem::path model;
  std::filesystem::path tokens;
  int num_threads = 2;
  std::string language = "auto";  // auto / zh / en / ja / ko / yue
  bool use_itn = true;            // 逆文本正规化(数字、日期等)
};

// vits(melo-tts)离线合成
struct TtsConfig {
  std::filesystem::path model;
  std::filesystem::path lexicon;
  std::filesystem::path tokens;
  std::filesystem::path dict_dir;  // jieba 词典目录(新版为兼容字段)
  int num_threads = 2;
  int speaker_id = 0;
  float speed = 1.0f;
};

// silero VAD
struct VadConfig {
  std::filesystem::path model;
  float threshold = 0.5f;
  float min_silence_seconds = 0.5f;
  float min_speech_seconds = 0.25f;
  float max_speech_seconds = 20.0f;
  int sample_rate = 16000;
};

struct AppConfig {
  LlmConfig llm;
  AsrConfig asr;
  TtsConfig tts;
  VadConfig vad;
  std::string system_prompt =
      "你是\"回声\"(Echo), 一个完全离线运行的中文语音助手。"
      "回答保持简短、口语化。";

  // 从指定 JSON 文件加载; 相对路径(如模型路径)相对配置文件所在目录解析
  [[nodiscard]] static Result<AppConfig> LoadFile(const std::filesystem::path& file);

  // 依次在 当前目录 / exe 目录 / exe 上三级目录(build/bin/Release -> 仓库根)
  // 查找 echo.json
  [[nodiscard]] static Result<AppConfig> LoadDefault();
};

}  // namespace echo
