#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <atomic>
#include <deque>
#include <memory>
#include <string>

#include "server/server_context.h"

namespace echo::server {

// WebSocket 会话: 文字聊天 + 全双工语音 + 打断 + 角色/历史。
class WsSession : public std::enable_shared_from_this<WsSession> {
 public:
  WsSession(boost::beast::tcp_stream stream, ServerContext& ctx);
  ~WsSession();

  void Run(boost::beast::http::request<boost::beast::http::string_body> req);
  void Send(std::string payload, bool binary = false);

 private:
  void OnAccept(boost::beast::error_code ec);
  void DoRead();
  void OnRead(boost::beast::error_code ec, size_t bytes);
  void QueueWrite(std::string payload, bool binary);
  void DoWrite();
  void OnWrite(boost::beast::error_code ec, size_t bytes);

  void HandleText(const std::string& text);
  void HandleBinary(const void* data, size_t size);

  void StartChatTurn(std::string user_text, bool speak);
  void ReplyDirect(std::string text, bool speak, std::string tool_name);
  void InterruptCurrentTurn();
  void StartMic();
  void StopMic();
  void OnDisconnect();
  void Persist(std::string role, std::string content);
  void HandleSetRole(const std::string& role_id);
  void HandleNewSession();

  boost::beast::websocket::stream<boost::beast::tcp_stream> ws_;
  boost::beast::flat_buffer buffer_;

  struct OutMessage {
    std::string data;
    bool binary = false;
  };
  std::deque<OutMessage> outbox_;

  ServerContext& ctx_;
  std::string session_id_;
  std::string role_id_;

  bool mic_acquired_ = false;
  std::atomic<bool> busy_{false};
  std::shared_ptr<std::atomic<bool>> turn_cancelled_;
  std::shared_ptr<std::string> reply_buffer_;  // 当前轮助手回复拼装
};

}  // namespace echo::server
