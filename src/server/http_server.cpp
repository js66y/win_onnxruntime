#include "server/http_server.h"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <chrono>
#include <filesystem>
#include <string_view>

#include "common/log.h"
#include "server/ws_session.h"

namespace echo::server {

namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

std::string_view MimeType(const std::filesystem::path& file) {
  const auto ext = file.extension().string();
  if (ext == ".html") return "text/html; charset=utf-8";
  if (ext == ".js") return "text/javascript; charset=utf-8";
  if (ext == ".css") return "text/css; charset=utf-8";
  if (ext == ".json") return "application/json";
  if (ext == ".svg") return "image/svg+xml";
  if (ext == ".png") return "image/png";
  if (ext == ".ico") return "image/x-icon";
  if (ext == ".wav") return "audio/wav";
  return "application/octet-stream";
}

http::response<http::string_body> TextResponse(
    const http::request<http::string_body>& req, http::status status,
    std::string_view body) {
  http::response<http::string_body> res{status, req.version()};
  res.set(http::field::content_type, "text/plain; charset=utf-8");
  res.keep_alive(req.keep_alive());
  res.body() = body;
  res.prepare_payload();
  return res;
}

// 静态文件托管: / -> index.html, 拒绝路径穿越
http::message_generator HandleRequest(http::request<http::string_body> req,
                                      const std::filesystem::path& web_root) {
  if (req.method() != http::verb::get && req.method() != http::verb::head) {
    return TextResponse(req, http::status::method_not_allowed,
                        "只支持 GET/HEAD");
  }

  std::string target(req.target());
  if (const auto query = target.find('?'); query != std::string::npos) {
    target.resize(query);
  }
  if (target.empty() || target.front() != '/' ||
      target.find("..") != std::string::npos) {
    return TextResponse(req, http::status::bad_request, "非法路径");
  }
  if (target == "/") target = "/index.html";

  const auto path = web_root / target.substr(1);

  http::file_body::value_type body;
  beast::error_code ec;
  body.open(path.string().c_str(), beast::file_mode::scan, ec);
  if (ec == beast::errc::no_such_file_or_directory) {
    return TextResponse(req, http::status::not_found, "404 Not Found");
  }
  if (ec) {
    return TextResponse(req, http::status::internal_server_error,
                        ec.message());
  }

  const auto size = body.size();
  http::response<http::file_body> res{
      std::piecewise_construct, std::make_tuple(std::move(body)),
      std::make_tuple(http::status::ok, req.version())};
  res.set(http::field::content_type, MimeType(path));
  res.content_length(size);
  res.keep_alive(req.keep_alive());
  return res;
}

// 一条 HTTP 连接: 循环处理请求; 遇到 WebSocket 升级则移交 WsSession
asio::awaitable<void> Session(tcp::socket socket,
                              std::filesystem::path web_root,
                              ChatService& chat) {
  beast::tcp_stream stream(std::move(socket));
  beast::flat_buffer buffer;

  try {
    for (;;) {
      stream.expires_after(std::chrono::seconds(30));
      http::request<http::string_body> req;
      co_await http::async_read(stream, buffer, req, asio::use_awaitable);

      if (beast::websocket::is_upgrade(req)) {
        // WsSession 通过 shared_ptr 自持生命周期
        std::make_shared<WsSession>(std::move(stream), chat)
            ->Run(std::move(req));
        co_return;
      }

      http::message_generator response =
          HandleRequest(std::move(req), web_root);
      const bool keep_alive = response.keep_alive();
      co_await beast::async_write(stream, std::move(response),
                                  asio::use_awaitable);
      if (!keep_alive) break;
    }
  } catch (const boost::system::system_error& e) {
    // 连接正常结束(对端关闭/超时)不算错误
    if (e.code() != http::error::end_of_stream &&
        e.code() != beast::error::timeout) {
      log::Debug("HTTP 会话异常结束: {}", e.code().message());
    }
  }

  boost::system::error_code ignored;
  stream.socket().shutdown(tcp::socket::shutdown_send, ignored);
}

asio::awaitable<void> Listen(tcp::acceptor acceptor,
                             std::filesystem::path web_root,
                             ChatService& chat) {
  for (;;) {
    auto [ec, socket] =
        co_await acceptor.async_accept(asio::as_tuple(asio::use_awaitable));
    if (ec == asio::error::operation_aborted) co_return;  // ioc 停止
    if (ec) {
      log::Warn("接受连接失败: {}", ec.message());
      continue;
    }
    asio::co_spawn(acceptor.get_executor(),
                   Session(std::move(socket), web_root, chat),
                   asio::detached);
  }
}

}  // namespace

Result<void> StartHttpServer(asio::io_context& ioc, const AppConfig& config,
                             ChatService& chat) {
  boost::system::error_code ec;
  const auto address = asio::ip::make_address(config.server.host, ec);
  if (ec) {
    return Fail(ErrorCode::kConfigInvalid, "无效的监听地址 {}: {}",
                config.server.host, ec.message());
  }
  const tcp::endpoint endpoint(address,
                               static_cast<unsigned short>(config.server.port));

  tcp::acceptor acceptor(ioc);
  acceptor.open(endpoint.protocol(), ec);
  if (!ec) acceptor.set_option(asio::socket_base::reuse_address(true), ec);
  if (!ec) acceptor.bind(endpoint, ec);
  if (!ec) acceptor.listen(asio::socket_base::max_listen_connections, ec);
  if (ec) {
    return Fail(ErrorCode::kIo, "监听 {}:{} 失败: {}", config.server.host,
                config.server.port, ec.message());
  }

  if (!std::filesystem::exists(config.server.web_root / "index.html")) {
    log::Warn("web 目录缺少 index.html: {}",
              config.server.web_root.string());
  }

  asio::co_spawn(ioc,
                 Listen(std::move(acceptor), config.server.web_root, chat),
                 asio::detached);
  return {};
}

}  // namespace echo::server
