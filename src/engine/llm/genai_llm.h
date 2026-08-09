#pragma once

#include <generator>
#include <memory>
#include <string>
#include <string_view>

#include "common/config.h"
#include "common/error.h"

namespace echo::engine {

// 单轮生成的性能统计
struct LlmTurnStats {
  int prompt_tokens = 0;         // 本轮追加的提示词 token 数
  int new_tokens = 0;            // 本轮生成的 token 数
  double first_token_seconds = 0.0;
  double total_seconds = 0.0;

  [[nodiscard]] double TokensPerSecond() const {
    return total_seconds > 0.0 ? new_tokens / total_seconds : 0.0;
  }
};

// onnxruntime-genai 的 RAII 封装。
// - pimpl: 上层不需要 include ort_genai.h
// - 会话状态保存在 KV cache 中, 多轮对话只追加增量 token
// - Chat() 返回协程 generator, 边生成边产出解码后的文本片段
// - RequestStop() 可从其他线程(如 Ctrl+C 处理器)安全调用
class GenAiLlm {
 public:
  [[nodiscard]] static Result<std::unique_ptr<GenAiLlm>> Create(LlmConfig config);

  ~GenAiLlm();
  GenAiLlm(GenAiLlm&&) noexcept;
  GenAiLlm& operator=(GenAiLlm&&) noexcept;
  GenAiLlm(const GenAiLlm&) = delete;
  GenAiLlm& operator=(const GenAiLlm&) = delete;

  // 以系统提示词开启会话(编码后进入 KV cache)
  [[nodiscard]] Result<void> StartSession(std::string_view system_prompt);

  // 流式对话: 逐片产出模型回复。生成中途出错或被打断时,
  // 会自动回退到本轮之前的会话状态并结束产出。
  [[nodiscard]] std::generator<std::string> Chat(std::string user_text);

  // 请求终止当前生成(线程安全, 可在信号处理器中调用)
  void RequestStop();

  // 清空聊天历史, 回到系统提示词之后的状态
  [[nodiscard]] Result<void> ResetSession();

  [[nodiscard]] const LlmTurnStats& last_stats() const;

 private:
  GenAiLlm();

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace echo::engine
