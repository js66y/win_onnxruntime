#pragma once

#include <chrono>
#include <cstdio>
#include <format>
#include <mutex>
#include <string_view>
#include <utility>

// 轻量日志: 变参模板 + std::format, 输出到 stderr(stdout 留给聊天内容)。
namespace echo::log {

enum class Level { kDebug = 0, kInfo, kWarn, kError };

inline Level g_min_level = Level::kInfo;

namespace detail {

inline void Write(Level level, std::string_view message) {
  static std::mutex mutex;
  static constexpr std::string_view kNames[] = {"DEBUG", "INFO", "WARN", "ERROR"};

  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  localtime_s(&tm, &time);

  const auto line =
      std::format("[{:02}:{:02}:{:02}] [{}] {}\n", tm.tm_hour, tm.tm_min,
                  tm.tm_sec, kNames[std::to_underlying(level)], message);

  std::scoped_lock lock(mutex);
  std::fputs(line.c_str(), stderr);
}

}  // namespace detail

template <class... Args>
void Log(Level level, std::format_string<Args...> fmt, Args&&... args) {
  if (level < g_min_level) return;
  detail::Write(level, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void Debug(std::format_string<Args...> fmt, Args&&... args) {
  Log(Level::kDebug, fmt, std::forward<Args>(args)...);
}
template <class... Args>
void Info(std::format_string<Args...> fmt, Args&&... args) {
  Log(Level::kInfo, fmt, std::forward<Args>(args)...);
}
template <class... Args>
void Warn(std::format_string<Args...> fmt, Args&&... args) {
  Log(Level::kWarn, fmt, std::forward<Args>(args)...);
}
template <class... Args>
void Error(std::format_string<Args...> fmt, Args&&... args) {
  Log(Level::kError, fmt, std::forward<Args>(args)...);
}

}  // namespace echo::log
