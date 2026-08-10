#include "storage/session_store.h"

#include <chrono>
#include <format>
#include <random>

#include "sqlite3.h"

namespace echo::storage {

namespace {

int64_t NowUnix() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string NewId() {
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<uint64_t> dist;
  return std::format("{:016x}{:016x}", dist(rng), dist(rng));
}

}  // namespace

struct SessionStore::Impl {
  sqlite3* db = nullptr;
  mutable std::mutex mutex;

  ~Impl() {
    if (db) sqlite3_close(db);
  }

  Result<void> Exec(const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
      std::string message = err ? err : "unknown";
      sqlite3_free(err);
      return Fail(ErrorCode::kIo, "SQL 执行失败: {}", message);
    }
    return {};
  }
};

SessionStore::SessionStore() : impl_(std::make_unique<Impl>()) {}
SessionStore::~SessionStore() = default;

Result<std::unique_ptr<SessionStore>> SessionStore::Open(
    const std::filesystem::path& db_path) {
  std::error_code ec;
  std::filesystem::create_directories(db_path.parent_path(), ec);

  auto store = std::unique_ptr<SessionStore>(new SessionStore());
  if (sqlite3_open(db_path.string().c_str(), &store->impl_->db) != SQLITE_OK) {
    return Fail(ErrorCode::kIo, "无法打开数据库 {}: {}", db_path.string(),
                sqlite3_errmsg(store->impl_->db));
  }

  constexpr const char* kSchema = R"sql(
    PRAGMA journal_mode=WAL;
    CREATE TABLE IF NOT EXISTS sessions (
      id TEXT PRIMARY KEY,
      role_id TEXT NOT NULL,
      title TEXT NOT NULL,
      created_at INTEGER NOT NULL,
      updated_at INTEGER NOT NULL
    );
    CREATE TABLE IF NOT EXISTS messages (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      session_id TEXT NOT NULL,
      role TEXT NOT NULL,
      content TEXT NOT NULL,
      created_at INTEGER NOT NULL,
      FOREIGN KEY(session_id) REFERENCES sessions(id)
    );
    CREATE INDEX IF NOT EXISTS idx_messages_session
      ON messages(session_id, id);
  )sql";

  if (auto ok = store->impl_->Exec(kSchema); !ok) return std::unexpected(ok.error());
  return store;
}

Result<Session> SessionStore::CreateSession(std::string role_id,
                                            std::string title) {
  std::scoped_lock lock(impl_->mutex);
  Session session{
      .id = NewId(),
      .role_id = std::move(role_id),
      .title = std::move(title),
      .created_at = NowUnix(),
      .updated_at = NowUnix(),
  };

  sqlite3_stmt* stmt = nullptr;
  constexpr const char* kSql =
      "INSERT INTO sessions(id, role_id, title, created_at, updated_at) "
      "VALUES(?,?,?,?,?)";
  if (sqlite3_prepare_v2(impl_->db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
    return Fail(ErrorCode::kIo, "prepare 失败: {}", sqlite3_errmsg(impl_->db));
  }
  sqlite3_bind_text(stmt, 1, session.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, session.role_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, session.title.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 4, session.created_at);
  sqlite3_bind_int64(stmt, 5, session.updated_at);
  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    return Fail(ErrorCode::kIo, "创建会话失败: {}", sqlite3_errmsg(impl_->db));
  }
  return session;
}

Result<std::vector<Session>> SessionStore::ListSessions(int limit) const {
  std::scoped_lock lock(impl_->mutex);
  sqlite3_stmt* stmt = nullptr;
  constexpr const char* kSql =
      "SELECT id, role_id, title, created_at, updated_at "
      "FROM sessions ORDER BY updated_at DESC LIMIT ?";
  if (sqlite3_prepare_v2(impl_->db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
    return Fail(ErrorCode::kIo, "prepare 失败: {}", sqlite3_errmsg(impl_->db));
  }
  sqlite3_bind_int(stmt, 1, limit);

  std::vector<Session> sessions;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    sessions.push_back(Session{
        .id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)),
        .role_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)),
        .title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
        .created_at = sqlite3_column_int64(stmt, 3),
        .updated_at = sqlite3_column_int64(stmt, 4),
    });
  }
  sqlite3_finalize(stmt);
  return sessions;
}

Result<std::vector<Message>> SessionStore::ListMessages(
    const std::string& session_id, int limit) const {
  std::scoped_lock lock(impl_->mutex);
  sqlite3_stmt* stmt = nullptr;
  constexpr const char* kSql =
      "SELECT id, role, content, created_at FROM messages "
      "WHERE session_id=? ORDER BY id ASC LIMIT ?";
  if (sqlite3_prepare_v2(impl_->db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
    return Fail(ErrorCode::kIo, "prepare 失败: {}", sqlite3_errmsg(impl_->db));
  }
  sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 2, limit);

  std::vector<Message> messages;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    messages.push_back(Message{
        .id = sqlite3_column_int64(stmt, 0),
        .role = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)),
        .content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
        .created_at = sqlite3_column_int64(stmt, 3),
    });
  }
  sqlite3_finalize(stmt);
  return messages;
}

Result<void> SessionStore::AppendMessage(const std::string& session_id,
                                         std::string role,
                                         std::string content) {
  std::scoped_lock lock(impl_->mutex);
  const auto now = NowUnix();

  sqlite3_stmt* stmt = nullptr;
  constexpr const char* kInsert =
      "INSERT INTO messages(session_id, role, content, created_at) "
      "VALUES(?,?,?,?)";
  if (sqlite3_prepare_v2(impl_->db, kInsert, -1, &stmt, nullptr) != SQLITE_OK) {
    return Fail(ErrorCode::kIo, "prepare 失败: {}", sqlite3_errmsg(impl_->db));
  }
  sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, role.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 4, now);
  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    return Fail(ErrorCode::kIo, "写入消息失败: {}", sqlite3_errmsg(impl_->db));
  }

  // 用首条用户消息当标题
  constexpr const char* kTouch =
      "UPDATE sessions SET updated_at=?,"
      " title=CASE WHEN title='新对话' AND ?='user' "
      " THEN substr(?,1,32) ELSE title END WHERE id=?";
  if (sqlite3_prepare_v2(impl_->db, kTouch, -1, &stmt, nullptr) != SQLITE_OK) {
    return Fail(ErrorCode::kIo, "prepare 失败: {}", sqlite3_errmsg(impl_->db));
  }
  sqlite3_bind_int64(stmt, 1, now);
  sqlite3_bind_text(stmt, 2, role.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, session_id.c_str(), -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    return Fail(ErrorCode::kIo, "更新会话失败: {}", sqlite3_errmsg(impl_->db));
  }
  return {};
}

Result<void> SessionStore::SetSessionRole(const std::string& session_id,
                                          std::string role_id) {
  std::scoped_lock lock(impl_->mutex);
  sqlite3_stmt* stmt = nullptr;
  constexpr const char* kSql =
      "UPDATE sessions SET role_id=?, updated_at=? WHERE id=?";
  if (sqlite3_prepare_v2(impl_->db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
    return Fail(ErrorCode::kIo, "prepare 失败: {}", sqlite3_errmsg(impl_->db));
  }
  sqlite3_bind_text(stmt, 1, role_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 2, NowUnix());
  sqlite3_bind_text(stmt, 3, session_id.c_str(), -1, SQLITE_TRANSIENT);
  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    return Fail(ErrorCode::kIo, "更新角色失败: {}", sqlite3_errmsg(impl_->db));
  }
  return {};
}

Result<void> SessionStore::TouchSession(const std::string& session_id,
                                        std::string title) {
  std::scoped_lock lock(impl_->mutex);
  sqlite3_stmt* stmt = nullptr;
  if (title.empty()) {
    constexpr const char* kSql =
        "UPDATE sessions SET updated_at=? WHERE id=?";
    if (sqlite3_prepare_v2(impl_->db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
      return Fail(ErrorCode::kIo, "prepare 失败: {}",
                  sqlite3_errmsg(impl_->db));
    }
    sqlite3_bind_int64(stmt, 1, NowUnix());
    sqlite3_bind_text(stmt, 2, session_id.c_str(), -1, SQLITE_TRANSIENT);
  } else {
    constexpr const char* kSql =
        "UPDATE sessions SET updated_at=?, title=? WHERE id=?";
    if (sqlite3_prepare_v2(impl_->db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
      return Fail(ErrorCode::kIo, "prepare 失败: {}",
                  sqlite3_errmsg(impl_->db));
    }
    sqlite3_bind_int64(stmt, 1, NowUnix());
    sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, session_id.c_str(), -1, SQLITE_TRANSIENT);
  }
  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    return Fail(ErrorCode::kIo, "更新会话失败: {}", sqlite3_errmsg(impl_->db));
  }
  return {};
}

}  // namespace echo::storage
