#pragma once

#include <boost/asio/io_context.hpp>

#include "common/error.h"
#include "server/server_context.h"

namespace echo::server {

// HTTP 静态托管 + /api/* + WebSocket 升级
[[nodiscard]] Result<void> StartHttpServer(boost::asio::io_context& ioc,
                                           ServerContext& ctx);

}  // namespace echo::server
