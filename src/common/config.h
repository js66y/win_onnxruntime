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

struct AppConfig {
  LlmConfig llm;
  std::string system_prompt =
      "你是\"回声\"(Echo), 一个完全离线运行的中文语音助手。"
      "回答保持简短、口语化。";

  // 从指定 JSON 文件加载; 相对路径(如 model_dir)相对配置文件所在目录解析
  [[nodiscard]] static Result<AppConfig> LoadFile(const std::filesystem::path& file);

  // 依次在 当前目录 / exe 目录 / exe 上三级目录(build/bin/Release -> 仓库根)
  // 查找 echo.json
  [[nodiscard]] static Result<AppConfig> LoadDefault();
};

}  // namespace echo
