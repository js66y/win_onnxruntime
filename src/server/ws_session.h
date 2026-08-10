#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <atomic>
#include <deque>
#include <memory>
#include <string>

#include "server/chat_service.h"
#include "server/speech_service.h"

namespace echo::server {

// 一条 WebSocket 连接 = 一个聊天会话(M4: 文字聊天 + 实时语音)。
//
// 文本帧(JSON):
//   上行: {"type":"chat","text"} | {"type":"stop"} | {"type":"reset"}
//         | {"type":"mic_start"} | {"type":"mic_stop"}
//   下行: {"type":"hello","model","voice","tts_sample_rate"}
//         | {"type":"llm_delta","text"} | {"type":"llm_end",...统计}
//         | {"type":"mic_ready"} | {"type":"speech_start"}
//         | {"type":"asr_text","text","lang"} | {"type":"tts_end"}
//         | {"type":"reset_done"} | {"type":"error","message"}
//
// 二进制帧(4 字节头 + PCM16LE 单声道):
//   上行 [0x01][3B 保留] 16kHz 麦克风音频
//   下行 [0x02][3B 保留] TTS 音频(采样率见 hello.tts_sample_rate)
//
// 线程模型: 网络读写只在 io 线程; LLM/语音回调经 Send() 内部的
// asio::post 切回 io 线程; busy_ 用原子量在线程间共享。
class WsSession : public std::enable_shared_from_this<WsSession> {
 public:
  WsSession(boost::beast::tcp_stream stream, ChatService& chat,
            SpeechService* speech);
  ~WsSession();

  void Run(boost::beast::http::request<boost::beast::http::string_body> req);

  // 线程安全: 任意线程可调用, 内部切回 io 线程排队发送
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

  // 开启一轮对话; speak=true 时回答同时逐句合成语音下行
  void StartChatTurn(std::string user_text, bool speak);
  void StartMic();
  void StopMic();
  void OnDisconnect();

  boost::beast::websocket::stream<boost::beast::tcp_stream> ws_;
  boost::beast::flat_buffer buffer_;

  struct OutMessage {
    std::string data;
    bool binary = false;
  };
  std::deque<OutMessage> outbox_;

  ChatService& chat_;
  SpeechService* speech_;  // 可为空(语音模型缺失时退化为纯文字)

  bool mic_acquired_ = false;              // io 线程内使用
  std::atomic<bool> busy_{false};          // 一轮问答进行中(生成或播报)
  std::shared_ptr<std::atomic<bool>> turn_cancelled_;  // 当前轮的打断标志
};

}  // namespace echo::server
