#pragma once

#include <functional>
#include <memory>
#include <string>

#include "common/config.h"
#include "common/error.h"
#include "engine/llm/genai_llm.h"

namespace echo::server {

// LLM 聊天服务: 拥有模型与一条专职推理线程, 请求排队串行执行。
// 回调在推理线程上触发, 调用方负责切回自己的执行器(如 asio::post)。
class ChatService {
 public:
  struct Callbacks {
    std::function<void(std::string piece)> on_delta;
    // 一轮生成结束(自然结束或被打断)
    std::function<void(engine::LlmTurnStats stats)> on_done;
    std::function<void(std::string message)> on_error;
  };

  // 加载模型并启动工作线程(阻塞直到模型就绪)
  [[nodiscard]] static Result<std::unique_ptr<ChatService>> Create(
      const AppConfig& config);

  ~ChatService();
  ChatService(const ChatService&) = delete;
  ChatService& operator=(const ChatService&) = delete;

  // 提交一轮对话(排队, 不阻塞)
  void SubmitChat(std::string user_text, Callbacks callbacks);

  // 提交清空会话(排队); done(true) 表示成功
  void SubmitReset(std::function<void(bool ok)> done);

  // 打断当前正在进行的生成(线程安全, 立即生效)
  void RequestStop();

  [[nodiscard]] const std::string& model_name() const { return model_name_; }

 private:
  ChatService();

  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::string model_name_;
};

}  // namespace echo::server
