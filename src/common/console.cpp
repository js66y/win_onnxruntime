#include "common/console.h"

#include <windows.h>

#include <io.h>

#include <cstdio>
#include <iostream>

namespace echo::console {

void Init() {
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);

  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD mode = 0;
  if (GetConsoleMode(out, &mode)) {
    SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  }
}

std::optional<std::string> ReadLineUtf8() {
  // 交互式控制台: 必须用宽字符 API 读取, 否则中文会按 ANSI 代码页乱码
  if (_isatty(_fileno(stdin))) {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    wchar_t buffer[4096];
    DWORD read = 0;
    if (!ReadConsoleW(in, buffer, static_cast<DWORD>(std::size(buffer)), &read,
                      nullptr)) {
      return std::nullopt;
    }
    // Ctrl+C 会让 ReadConsoleW 返回 0 字符: 当作空行处理而不是退出
    std::wstring_view line(buffer, read);
    while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n')) {
      line.remove_suffix(1);
    }
    if (!line.empty() && line.front() == L'\x1a') {  // Ctrl+Z = EOF
      return std::nullopt;
    }

    if (line.empty()) return std::string{};
    const int size = WideCharToMultiByte(CP_UTF8, 0, line.data(),
                                         static_cast<int>(line.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
                        utf8.data(), size, nullptr, nullptr);
    return utf8;
  }

  // 管道/重定向: 假定输入本身就是 UTF-8
  std::string line;
  if (!std::getline(std::cin, line)) return std::nullopt;
  if (!line.empty() && line.back() == '\r') line.pop_back();
  return line;
}

}  // namespace echo::console
