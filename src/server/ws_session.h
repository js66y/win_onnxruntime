#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <deque>
#include <memory>
#include <string>

#include "server/chat_service.h"

namespace echo::server {

// 一条 WebSocket 连接 = 一个聊天会话(M3: 所有连接共享同一个 LLM 上下文)。
//
// 协议(文本帧, JSON):
//   客户端 -> 服务端: {"type":"chat","text":..} | {"type":"stop"} | {"type":"reset"}
//   服务端 -> 客户端: {"type":"hello","model":..} | {"type":"llm_delta","text":..}
//                     | {"type":"llm_end", ...统计字段} | {"type":"reset_done"}
//                     | {"type":"error","message":..}
//
// 线程模型: 所有网络读写都发生在 io_context 线程; LLM 回调通过
// asio::post 切回 io 线程后再入写队列, Send() 因此线程安全。
class WsSession : public std::enable_shared_from_this<WsSession> {
 public:
  WsSession(boost::beast::tcp_stream stream, ChatService& chat);

  // 接管一个已读到 upgrade 请求的连接
  void Run(boost::beast::http::request<boost::beast::http::string_body> req);

  // 线程安全: 任意线程可调用, 内部切回 io 线程排队发送
  void Send(std::string text);

 private:
  void OnAccept(boost::beast::error_code ec);
  void DoRead();
  void OnRead(boost::beast::error_code ec, size_t bytes);
  void QueueWrite(std::string text);
  void OnWrite(boost::beast::error_code ec, size_t bytes);
  void HandleMessage(const std::string& text);

  boost::beast::websocket::stream<boost::beast::tcp_stream> ws_;
  boost::beast::flat_buffer buffer_;
  std::deque<std::string> outbox_;
  ChatService& chat_;
};

}  // namespace echo::server
