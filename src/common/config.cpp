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
    // 注: path / absolute_path 会直接得到 absolute_path, 所以绝对路径也兼容
    llm.model_dir = base / std::filesystem::path(it->value("model_dir", ""));
    llm.max_length = it->value("max_length", llm.max_length);
    llm.do_sample = it->value("do_sample", llm.do_sample);
    llm.temperature = it->value("temperature", llm.temperature);
    llm.top_p = it->value("top_p", llm.top_p);
    llm.disable_thinking = it->value("disable_thinking", llm.disable_thinking);
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
