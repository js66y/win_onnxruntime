#include "server/chat_service.h"

#include <thread>
#include <variant>

#include "common/bounded_queue.h"
#include "common/log.h"

namespace echo::server {

namespace {

struct ChatTask {
  std::string user_text;
  ChatService::Callbacks callbacks;
};

struct ResetTask {
  std::function<void(bool ok)> done;
};

using Task = std::variant<ChatTask, ResetTask>;

}  // namespace

struct ChatService::Impl {
  std::unique_ptr<engine::GenAiLlm> llm;
  BoundedQueue<Task> queue{64};
  std::jthread worker;  // 最后声明, 析构时最先停(先停线程再释放 llm)

  void Run(std::stop_token stop) {
    while (auto task = queue.Pop(stop)) {
      std::visit([this](auto&& t) { Handle(std::move(t)); }, *std::move(task));
    }
  }

  void Handle(ChatTask task) {
    // generator 产出右值, 按值接收即移动构造
    for (std::string piece : llm->Chat(std::move(task.user_text))) {
      if (task.callbacks.on_delta) task.callbacks.on_delta(std::move(piece));
    }
    if (task.callbacks.on_done) task.callbacks.on_done(llm->last_stats());
  }

  void Handle(ResetTask task) {
    auto reset = llm->ResetSession();
    if (!reset) log::Error("{}", reset.error().message);
    if (task.done) task.done(reset.has_value());
  }
};

ChatService::ChatService() : impl_(std::make_unique<Impl>()) {}

ChatService::~ChatService() {
  if (impl_ && impl_->worker.joinable()) {
    impl_->worker.request_stop();
    // 打断可能正在进行的生成, 让 Pop/Chat 尽快返回
    impl_->llm->RequestStop();
  }
}

Result<std::unique_ptr<ChatService>> ChatService::Create(
    const AppConfig& config) {
  auto llm = engine::GenAiLlm::Create(config.llm);
  if (!llm) return std::unexpected(llm.error());
  if (auto started = (*llm)->StartSession(config.system_prompt); !started) {
    return std::unexpected(started.error());
  }

  auto service = std::unique_ptr<ChatService>(new ChatService());
  service->impl_->llm = std::move(*llm);
  service->model_name_ = config.llm.model_dir.filename().string();
  service->impl_->worker = std::jthread(
      [impl = service->impl_.get()](std::stop_token stop) { impl->Run(stop); });
  return service;
}

void ChatService::SubmitChat(std::string user_text, Callbacks callbacks) {
  const bool queued = impl_->queue.Push(
      ChatTask{std::move(user_text), std::move(callbacks)},
      impl_->worker.get_stop_token());
  if (!queued) log::Warn("聊天服务正在关闭, 丢弃请求");
}

void ChatService::SubmitReset(std::function<void(bool ok)> done) {
  impl_->queue.Push(ResetTask{std::move(done)},
                    impl_->worker.get_stop_token());
}

void ChatService::RequestStop() { impl_->llm->RequestStop(); }

}  // namespace echo::server
