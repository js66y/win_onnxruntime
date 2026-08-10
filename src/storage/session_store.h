#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "common/error.h"

namespace echo::storage {

struct Message {
  int64_t id = 0;
  std::string role;  // user / assistant / system / tool
  std::string content;
  int64_t created_at = 0;  // unix seconds
};

struct Session {
  std::string id;
  std::string role_id;
  std::string title;
  int64_t created_at = 0;
  int64_t updated_at = 0;
};

// SQLite 会话持久化(线程安全)。库文件默认 data/echo.db。
class SessionStore {
 public:
  [[nodiscard]] static Result<std::unique_ptr<SessionStore>> Open(
      const std::filesystem::path& db_path);

  ~SessionStore();
  SessionStore(const SessionStore&) = delete;
  SessionStore& operator=(const SessionStore&) = delete;

  [[nodiscard]] Result<Session> CreateSession(std::string role_id,
                                              std::string title = "新对话");

  [[nodiscard]] Result<std::vector<Session>> ListSessions(
      int limit = 50) const;

  [[nodiscard]] Result<std::vector<Message>> ListMessages(
      const std::string& session_id, int limit = 200) const;

  [[nodiscard]] Result<void> AppendMessage(const std::string& session_id,
                                           std::string role,
                                           std::string content);

  [[nodiscard]] Result<void> SetSessionRole(const std::string& session_id,
                                            std::string role_id);

  [[nodiscard]] Result<void> TouchSession(const std::string& session_id,
                                          std::string title = {});

 private:
  SessionStore();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace echo::storage
