#include "common/config.h"

#include <windows.h>

#include <fstream>

#include <nlohmann/json.hpp>

namespace echo {

namespace {

std::filesystem::path ExecutableDir() {
  wchar_t buffer[MAX_PATH]{};
  GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  return std::filesystem::path(buffer).parent_path();
}

// 相对路径基于配置文件目录解析; 绝对路径原样返回; 空串返回空 path
std::filesystem::path ResolvePath(const std::filesystem::path& base,
                                  const std::string& value) {
  if (value.empty()) return {};
  return base / std::filesystem::path(value);
}

}  // namespace

Result<AppConfig> AppConfig::LoadFile(const std::filesystem::path& file) {
  std::ifstream in(file);
  if (!in) {
    return Fail(ErrorCode::kConfigNotFound, "无法打开配置文件: {}",
                file.string());
  }

  const auto json = nlohmann::json::parse(in, /*cb=*/nullptr,
                                          /*allow_exceptions=*/false);
  if (json.is_discarded()) {
    return Fail(ErrorCode::kConfigInvalid, "配置文件不是合法 JSON: {}",
                file.string());
  }

  AppConfig config;
  const auto base = std::filesystem::absolute(file).parent_path();

  if (const auto it = json.find("llm"); it != json.end()) {
    auto& llm = config.llm;
    llm.model_dir = ResolvePath(base, it->value("model_dir", ""));
    llm.max_length = it->value("max_length", llm.max_length);
    llm.do_sample = it->value("do_sample", llm.do_sample);
    llm.temperature = it->value("temperature", llm.temperature);
    llm.top_p = it->value("top_p", llm.top_p);
    llm.disable_thinking = it->value("disable_thinking", llm.disable_thinking);
  }

  if (const auto it = json.find("asr"); it != json.end()) {
    auto& asr = config.asr;
    asr.model = ResolvePath(base, it->value("model", ""));
    asr.tokens = ResolvePath(base, it->value("tokens", ""));
    asr.num_threads = it->value("num_threads", asr.num_threads);
    asr.language = it->value("language", asr.language);
    asr.use_itn = it->value("use_itn", asr.use_itn);
  }

  if (const auto it = json.find("tts"); it != json.end()) {
    auto& tts = config.tts;
    tts.model = ResolvePath(base, it->value("model", ""));
    tts.lexicon = ResolvePath(base, it->value("lexicon", ""));
    tts.tokens = ResolvePath(base, it->value("tokens", ""));
    tts.dict_dir = ResolvePath(base, it->value("dict_dir", ""));
    tts.num_threads = it->value("num_threads", tts.num_threads);
    tts.speaker_id = it->value("speaker_id", tts.speaker_id);
    tts.speed = it->value("speed", tts.speed);
  }

  if (const auto it = json.find("vad"); it != json.end()) {
    auto& vad = config.vad;
    vad.model = ResolvePath(base, it->value("model", ""));
    vad.threshold = it->value("threshold", vad.threshold);
    vad.min_silence_seconds =
        it->value("min_silence_seconds", vad.min_silence_seconds);
    vad.min_speech_seconds =
        it->value("min_speech_seconds", vad.min_speech_seconds);
    vad.max_speech_seconds =
        it->value("max_speech_seconds", vad.max_speech_seconds);
    vad.sample_rate = it->value("sample_rate", vad.sample_rate);
  }

  config.system_prompt = json.value("system_prompt", config.system_prompt);

  if (config.llm.model_dir.empty()) {
    return Fail(ErrorCode::kConfigInvalid, "{} 缺少 llm.model_dir 字段",
                file.string());
  }
  return config;
}

Result<AppConfig> AppConfig::LoadDefault() {
  const auto exe_dir = ExecutableDir();
  const std::filesystem::path candidates[] = {
      std::filesystem::current_path() / "echo.json",
      exe_dir / "echo.json",
      exe_dir / ".." / ".." / ".." / "echo.json",
  };
  for (const auto& path : candidates) {
    if (std::filesystem::exists(path)) return LoadFile(path);
  }
  return Fail(ErrorCode::kConfigNotFound,
              "找不到 echo.json(查找了当前目录、exe 目录和仓库根目录)");
}

}  // namespace echo
