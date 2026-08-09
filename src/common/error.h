#pragma once

#include <expected>
#include <format>
#include <string>
#include <utility>

namespace echo {

enum class ErrorCode {
  kConfigNotFound,
  kConfigInvalid,
  kModelLoadFailed,
  kGenerationFailed,
  kIo,
};

struct Error {
  ErrorCode code;
  std::string message;
};

// 项目统一的错误处理方式: 边界层捕获第三方库异常, 内部用 Result 传递。
template <class T>
using Result = std::expected<T, Error>;

// 用法: return Fail(ErrorCode::kConfigNotFound, "找不到配置文件: {}", path);
template <class... Args>
[[nodiscard]] std::unexpected<Error> Fail(ErrorCode code,
                                          std::format_string<Args...> fmt,
                                          Args&&... args) {
  return std::unexpected(Error{code, std::format(fmt, std::forward<Args>(args)...)});
}

}  // namespace echo
