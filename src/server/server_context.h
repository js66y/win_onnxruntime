#pragma once

#include "common/config.h"
#include "dialog/tools.h"
#include "server/chat_service.h"
#include "server/speech_service.h"
#include "storage/session_store.h"

namespace echo::server {

// 服务端共享依赖, 由 main 组装后交给 HTTP/WebSocket 层
struct ServerContext {
  const AppConfig* config = nullptr;
  ChatService* chat = nullptr;
  SpeechService* speech = nullptr;  // 可为 null
  storage::SessionStore* store = nullptr;
  dialog::ToolRegistry tools;
};

}  // namespace echo::server
