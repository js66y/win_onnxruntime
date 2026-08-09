#include "server/ws_session.h"

#include <boost/asio/post.hpp>

#include <nlohmann/json.hpp>

#include "common/log.h"

namespace echo::server {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
using json = nlohmann::json;

WsSession::WsSession(beast::tcp_stream stream, ChatService& chat)
    : ws_(std::move(stream)), chat_(chat) {}

void WsSession::Run(
    beast::http::request<beast::http::string_body> req) {
  // WebSocket 有自己的 ping/pong 保活, 关闭 TCP 层超时
  beast::get_lowest_layer(ws_).expires_never();
  ws_.set_option(
      websocket::stream_base::timeout::suggested(beast::role_type::server));

  ws_.async_accept(req, beast::bind_front_handler(&WsSession::OnAccept,
                                                  shared_from_this()));
}

void WsSession::OnAccept(beast::error_code ec) {
  if (ec) {
    log::Warn("WebSocket 握手失败: {}", ec.message());
    return;
  }
  log::Info("WebSocket 连接建立");
  Send(json{{"type", "hello"}, {"model", chat_.model_name()}}.dump());
  DoRead();
}

void WsSession::DoRead() {
  ws_.async_read(buffer_, beast::bind_front_handler(&WsSession::OnRead,
                                                    shared_from_this()));
}

void WsSession::OnRead(beast::error_code ec, size_t /*bytes*/) {
  if (ec == websocket::error::closed || ec == beast::http::error::end_of_stream) {
    log::Info("WebSocket 连接关闭");
    chat_.RequestStop();  // 单用户场景: 人走了就不用继续生成了
    return;
  }
  if (ec) {
    log::Warn("WebSocket 读取失败: {}", ec.message());
    chat_.RequestStop();
    return;
  }

  if (ws_.got_text()) {
    HandleMessage(beast::buffers_to_string(buffer_.data()));
  }
  buffer_.consume(buffer_.size());
  DoRead();
}

void WsSession::HandleMessage(const std::string& text) {
  const auto message = json::parse(text, /*cb=*/nullptr,
                                   /*allow_exceptions=*/false);
  const auto type = message.is_object() ? message.value("type", "") : "";

  if (type == "chat") {
    auto user_text = message.value("text", "");
    if (user_text.empty()) return;

    // LLM 回调发生在推理线程, Send 内部会切回 io 线程
    std::weak_ptr<WsSession> weak = shared_from_this();
    ChatService::Callbacks callbacks{
        .on_delta =
            [weak](std::string piece) {
              if (auto self = weak.lock()) {
                self->Send(json{{"type", "llm_delta"},
                                {"text", std::move(piece)}}
                               .dump());
              }
            },
        .on_done =
            [weak](engine::LlmTurnStats stats) {
              if (auto self = weak.lock()) {
                self->Send(json{{"type", "llm_end"},
                                {"prompt_tokens", stats.prompt_tokens},
                                {"new_tokens", stats.new_tokens},
                                {"first_token_seconds",
                                 stats.first_token_seconds},
                                {"tokens_per_second",
                                 stats.TokensPerSecond()}}
                               .dump());
              }
            },
        .on_error =
            [weak](std::string error) {
              if (auto self = weak.lock()) {
                self->Send(json{{"type", "error"},
                                {"message", std::move(error)}}
                               .dump());
              }
            },
    };
    chat_.SubmitChat(std::move(user_text), std::move(callbacks));
    return;
  }

  if (type == "stop") {
    chat_.RequestStop();
    return;
  }

  if (type == "reset") {
    std::weak_ptr<WsSession> weak = shared_from_this();
    chat_.SubmitReset([weak](bool ok) {
      if (auto self = weak.lock()) {
        self->Send(ok ? json{{"type", "reset_done"}}.dump()
                      : json{{"type", "error"},
                             {"message", "清空会话失败"}}
                            .dump());
      }
    });
    return;
  }

  log::Warn("未知的 WebSocket 消息类型: {}", type);
}

void WsSession::Send(std::string text) {
  boost::asio::post(
      ws_.get_executor(),
      [self = shared_from_this(), t = std::move(text)]() mutable {
        self->QueueWrite(std::move(t));
      });
}

void WsSession::QueueWrite(std::string text) {
  outbox_.push_back(std::move(text));
  if (outbox_.size() > 1) return;  // 已有写操作在途, OnWrite 会接力

  ws_.text(true);
  ws_.async_write(boost::asio::buffer(outbox_.front()),
                  beast::bind_front_handler(&WsSession::OnWrite,
                                            shared_from_this()));
}

void WsSession::OnWrite(beast::error_code ec, size_t /*bytes*/) {
  if (ec) {
    log::Warn("WebSocket 发送失败: {}", ec.message());
    return;
  }
  outbox_.pop_front();
  if (!outbox_.empty()) {
    ws_.text(true);
    ws_.async_write(boost::asio::buffer(outbox_.front()),
                    beast::bind_front_handler(&WsSession::OnWrite,
                                              shared_from_this()));
  }
}

}  // namespace echo::server
