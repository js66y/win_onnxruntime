#pragma once

#include <boost/asio/io_context.hpp>

#include "common/config.h"
#include "common/error.h"
#include "server/chat_service.h"

namespace echo::server {

// 在 ioc 上启动 HTTP + WebSocket 服务:
// - GET /*        -> 托管 config.server.web_root 下的静态文件
// - GET /ws (升级) -> WebSocket 聊天会话
// 返回错误当且仅当端口监听失败。
[[nodiscard]] Result<void> StartHttpServer(boost::asio::io_context& ioc,
                                           const AppConfig& config,
                                           ChatService& chat);

}  // namespace echo::server
