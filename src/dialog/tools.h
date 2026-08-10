#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace echo::dialog {

struct ToolResult {
  std::string tool_name;
  std::string display;  // 直接作为助手回复展示/朗读
};

// 本地工具: 不依赖 LLM, 用关键词/表达式匹配用户意图。
// 0.6B 小模型工具调用不稳定, 所以走确定性路由更可靠。
class ToolRegistry {
 public:
  using Handler = std::function<std::string(std::string_view args)>;

  struct Tool {
    std::string name;
    std::string description;
    std::vector<std::string> keywords;  // 命中任一即可
    Handler handler;
  };

  ToolRegistry();

  // 若命中工具则返回结果, 否则 nullopt(交给 LLM)
  [[nodiscard]] std::optional<ToolResult> TryHandle(
      std::string_view user_text) const;

  [[nodiscard]] const std::vector<Tool>& tools() const { return tools_; }

  // 拼进 system prompt, 让模型知道有哪些本地能力(也可口述引导用户)
  [[nodiscard]] std::string PromptHint() const;

 private:
  std::vector<Tool> tools_;
};

}  // namespace echo::dialog
