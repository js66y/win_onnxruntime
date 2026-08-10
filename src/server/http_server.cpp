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

#include <nlohmann/json.hpp>

#include "common/log.h"
#include "server/ws_session.h"

namespace echo::server {

namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;
using json = nlohmann::json;

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
    std::string_view body, std::string_view content_type = "text/plain; charset=utf-8") {
  http::response<http::string_body> res{status, req.version()};
  res.set(http::field::content_type, content_type);
  res.keep_alive(req.keep_alive());
  res.body() = body;
  res.prepare_payload();
  return res;
}

http::response<http::string_body> JsonResponse(
    const http::request<http::string_body>& req, http::status status,
    const json& body) {
  return TextResponse(req, status, body.dump(), "application/json; charset=utf-8");
}

std::string QueryParam(std::string_view target, std::string_view key) {
  const auto q = target.find('?');
  if (q == std::string_view::npos) return {};
  std::string_view query = target.substr(q + 1);
  while (!query.empty()) {
    const auto amp = query.find('&');
    const auto part = query.substr(0, amp);
    const auto eq = part.find('=');
    if (eq != std::string_view::npos && part.substr(0, eq) == key) {
      return std::string(part.substr(eq + 1));
    }
    if (amp == std::string_view::npos) break;
    query.remove_prefix(amp + 1);
  }
  return {};
}

http::message_generator HandleApi(const http::request<http::string_body>& req,
                                  ServerContext& ctx) {
  std::string target(req.target());
  std::string path = target;
  if (const auto q = path.find('?'); q != std::string::npos) path.resize(q);

  if (path == "/api/roles") {
    json arr = json::array();
    for (const auto& role : ctx.config->roles) {
      arr.push_back({{"id", role.id}, {"name", role.name}});
    }
    return JsonResponse(req, http::status::ok,
                        {{"roles", arr}, {"active", ctx.config->active_role}});
  }

  if (path == "/api/sessions") {
    auto sessions = ctx.store->ListSessions();
    if (!sessions) {
      return JsonResponse(req, http::status::internal_server_error,
                          {{"error", sessions.error().message}});
    }
    json arr = json::array();
    for (const auto& s : *sessions) {
      arr.push_back({{"id", s.id},
                     {"role_id", s.role_id},
                     {"title", s.title},
                     {"updated_at", s.updated_at}});
    }
    return JsonResponse(req, http::status::ok, {{"sessions", arr}});
  }

  if (path == "/api/history") {
    const auto session_id = QueryParam(target, "session_id");
    if (session_id.empty()) {
      return JsonResponse(req, http::status::bad_request,
                          {{"error", "缺少 session_id"}});
    }
    auto messages = ctx.store->ListMessages(session_id);
    if (!messages) {
      return JsonResponse(req, http::status::internal_server_error,
                          {{"error", messages.error().message}});
    }
    json arr = json::array();
    for (const auto& m : *messages) {
      arr.push_back({{"role", m.role}, {"content", m.content}});
    }
    return JsonResponse(req, http::status::ok, {{"messages", arr}});
  }

  return JsonResponse(req, http::status::not_found, {{"error", "not found"}});
}

http::message_generator HandleRequest(http::request<http::string_body> req,
                                      ServerContext& ctx) {
  if (req.method() != http::verb::get && req.method() != http::verb::head) {
    return TextResponse(req, http::status::method_not_allowed,
                        "只支持 GET/HEAD");
  }

  std::string target(req.target());
  std::string path = target;
  if (const auto query = path.find('?'); query != std::string::npos) {
    path.resize(query);
  }
  if (path.empty() || path.front() != '/' ||
      path.find("..") != std::string::npos) {
    return TextResponse(req, http::status::bad_request, "非法路径");
  }

  if (path.starts_with("/api/")) {
    return HandleApi(req, ctx);
  }

  if (path == "/") path = "/index.html";
  const auto file = ctx.config->server.web_root / path.substr(1);

  http::file_body::value_type body;
  beast::error_code ec;
  body.open(file.string().c_str(), beast::file_mode::scan, ec);
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
  res.set(http::field::content_type, MimeType(file));
  res.content_length(size);
  res.keep_alive(req.keep_alive());
  return res;
}

asio::awaitable<void> Session(tcp::socket socket, ServerContext& ctx) {
  beast::tcp_stream stream(std::move(socket));
  beast::flat_buffer buffer;

  try {
    for (;;) {
      stream.expires_after(std::chrono::seconds(30));
      http::request<http::string_body> req;
      co_await http::async_read(stream, buffer, req, asio::use_awaitable);

      if (beast::websocket::is_upgrade(req)) {
        std::make_shared<WsSession>(std::move(stream), ctx)->Run(std::move(req));
        co_return;
      }

      http::message_generator response = HandleRequest(std::move(req), ctx);
      const bool keep_alive = response.keep_alive();
      co_await beast::async_write(stream, std::move(response),
                                  asio::use_awaitable);
      if (!keep_alive) break;
    }
  } catch (const boost::system::system_error& e) {
    if (e.code() != http::error::end_of_stream &&
        e.code() != beast::error::timeout) {
      log::Debug("HTTP 会话异常结束: {}", e.code().message());
    }
  }

  boost::system::error_code ignored;
  stream.socket().shutdown(tcp::socket::shutdown_send, ignored);
}

asio::awaitable<void> Listen(tcp::acceptor acceptor, ServerContext& ctx) {
  for (;;) {
    auto [ec, socket] =
        co_await acceptor.async_accept(asio::as_tuple(asio::use_awaitable));
    if (ec == asio::error::operation_aborted) co_return;
    if (ec) {
      log::Warn("接受连接失败: {}", ec.message());
      continue;
    }
    asio::co_spawn(acceptor.get_executor(), Session(std::move(socket), ctx),
                   asio::detached);
  }
}

}  // namespace

Result<void> StartHttpServer(asio::io_context& ioc, ServerContext& ctx) {
  boost::system::error_code ec;
  const auto& config = *ctx.config;
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

  asio::co_spawn(ioc, Listen(std::move(acceptor), ctx), asio::detached);
  return {};
}

}  // namespace echo::server
