#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "common/error.h"

namespace echo {

struct LlmConfig {
  std::filesystem::path model_dir;
  int max_length = 4096;
  bool do_sample = true;
  float temperature = 0.7f;
  float top_p = 0.8f;
  bool disable_thinking = true;
};

struct AsrConfig {
  std::filesystem::path model;
  std::filesystem::path tokens;
  int num_threads = 2;
  std::string language = "auto";
  bool use_itn = true;
};

struct TtsConfig {
  std::filesystem::path model;
  std::filesystem::path lexicon;
  std::filesystem::path tokens;
  std::filesystem::path dict_dir;
  int num_threads = 4;
  int speaker_id = 0;
  float speed = 1.0f;
};

struct VadConfig {
  std::filesystem::path model;
  float threshold = 0.5f;
  float min_silence_seconds = 0.3f;
  float min_speech_seconds = 0.25f;
  float max_speech_seconds = 20.0f;
  int sample_rate = 16000;
};

struct ServerConfig {
  std::string host = "127.0.0.1";
  int port = 8080;
  std::filesystem::path web_root;
  std::filesystem::path db_path;  // SQLite, 默认 data/echo.db
};

struct RoleConfig {
  std::string id;
  std::string name;
  std::string system_prompt;
};

struct AppConfig {
  LlmConfig llm;
  AsrConfig asr;
  TtsConfig tts;
  VadConfig vad;
  ServerConfig server;
  std::vector<RoleConfig> roles;
  std::string active_role = "echo";
  // 兼容旧配置: 无 roles 时用它作为默认角色提示词
  std::string system_prompt =
      "你是\"回声\"(Echo), 一个完全离线运行的中文语音助手。"
      "回答保持简短、口语化。";

  [[nodiscard]] const RoleConfig* FindRole(std::string_view id) const;
  [[nodiscard]] std::string ActiveSystemPrompt() const;

  [[nodiscard]] static Result<AppConfig> LoadFile(
      const std::filesystem::path& file);
  [[nodiscard]] static Result<AppConfig> LoadDefault();
};

}  // namespace echo
