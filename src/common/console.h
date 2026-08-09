#pragma once

#include <optional>
#include <string>

// Windows 控制台 UTF-8 支持: 中文输入走 ReadConsoleW, 输出走 UTF-8 代码页。
namespace echo::console {

// 设置控制台输入/输出为 UTF-8, 并启用 ANSI 颜色转义
void Init();

// 读取一行 UTF-8 文本(交互模式用 ReadConsoleW 保证中文输入正确;
// 重定向/管道输入退化为 std::getline)。返回 nullopt 表示 EOF。
std::optional<std::string> ReadLineUtf8();

}  // namespace echo::console
