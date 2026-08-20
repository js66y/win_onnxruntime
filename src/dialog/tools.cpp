#include "dialog/tools.h"

#include <chrono>
#include <cmath>
#include <cctype>
#include <format>
#include <optional>
#include <regex>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>
#else
#include <sys/sysinfo.h>
#include <unistd.h>
#endif

namespace echo::dialog {

namespace {

bool ContainsIgnoreCase(std::string_view text, std::string_view needle) {
  if (needle.empty() || text.size() < needle.size()) return false;
  // 中文关键词用精确子串; ASCII 关键词不区分大小写
  bool ascii = true;
  for (unsigned char c : needle) {
    if (c >= 0x80) {
      ascii = false;
      break;
    }
  }
  if (!ascii) return text.find(needle) != std::string_view::npos;

  for (size_t i = 0; i + needle.size() <= text.size(); ++i) {
    bool match = true;
    for (size_t j = 0; j < needle.size(); ++j) {
      if (std::tolower(static_cast<unsigned char>(text[i + j])) !=
          std::tolower(static_cast<unsigned char>(needle[j]))) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}

std::string NowString() {
  using clock = std::chrono::system_clock;
  const auto now = clock::now();
  const auto time = clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &time);
#else
  localtime_r(&time, &tm);
#endif
  constexpr const char* kWeekdays[] = {"日", "一", "二", "三",
                                       "四", "五", "六"};
  return std::format("现在是 {:04}-{:02}-{:02} 星期{} {:02}:{:02}:{:02}",
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                     kWeekdays[tm.tm_wday], tm.tm_hour, tm.tm_min, tm.tm_sec);
}

// 从用户文本里抠出算术表达式(只允许数字与 + - * / ( ) . 空格)
std::optional<std::string> ExtractExpression(std::string_view text) {
  static const std::regex kExpr(R"(([0-9+\-*/().\s]{3,}))");
  const std::string owned(text);
  std::smatch match;
  if (!std::regex_search(owned, match, kExpr)) return std::nullopt;
  auto expr = match[1].str();
  // 至少要有一个运算符才算"计算"
  if (expr.find_first_of("+-*/") == std::string::npos) return std::nullopt;
  return expr;
}

// 递归下降求值, 支持 + - * / 与括号
class ExprEval {
 public:
  explicit ExprEval(std::string expr) : expr_(std::move(expr)), i_(0) {}

  std::optional<double> Eval() {
    Skip();
    auto value = ParseExpr();
    Skip();
    if (!value || i_ != expr_.size()) return std::nullopt;
    return value;
  }

 private:
  void Skip() {
    while (i_ < expr_.size() && std::isspace(static_cast<unsigned char>(expr_[i_]))) {
      ++i_;
    }
  }

  std::optional<double> ParseExpr() {
    auto left = ParseTerm();
    if (!left) return std::nullopt;
    while (true) {
      Skip();
      if (i_ >= expr_.size()) return left;
      const char op = expr_[i_];
      if (op != '+' && op != '-') return left;
      ++i_;
      auto right = ParseTerm();
      if (!right) return std::nullopt;
      left = (op == '+') ? *left + *right : *left - *right;
    }
  }

  std::optional<double> ParseTerm() {
    auto left = ParseFactor();
    if (!left) return std::nullopt;
    while (true) {
      Skip();
      if (i_ >= expr_.size()) return left;
      const char op = expr_[i_];
      if (op != '*' && op != '/') return left;
      ++i_;
      auto right = ParseFactor();
      if (!right) return std::nullopt;
      if (op == '/' && std::fabs(*right) < 1e-12) return std::nullopt;
      left = (op == '*') ? *left * *right : *left / *right;
    }
  }

  std::optional<double> ParseFactor() {
    Skip();
    if (i_ >= expr_.size()) return std::nullopt;
    if (expr_[i_] == '+') {
      ++i_;
      return ParseFactor();
    }
    if (expr_[i_] == '-') {
      ++i_;
      auto v = ParseFactor();
      return v ? std::optional(-*v) : std::nullopt;
    }
    if (expr_[i_] == '(') {
      ++i_;
      auto v = ParseExpr();
      Skip();
      if (!v || i_ >= expr_.size() || expr_[i_] != ')') return std::nullopt;
      ++i_;
      return v;
    }
    size_t start = i_;
    while (i_ < expr_.size() &&
           (std::isdigit(static_cast<unsigned char>(expr_[i_])) ||
            expr_[i_] == '.')) {
      ++i_;
    }
    if (start == i_) return std::nullopt;
    try {
      return std::stod(expr_.substr(start, i_ - start));
    } catch (...) {
      return std::nullopt;
    }
  }

  std::string expr_;
  size_t i_;
};

std::string FormatNumber(double value) {
  if (std::fabs(value - std::round(value)) < 1e-9) {
    return std::format("{}", static_cast<long long>(std::llround(value)));
  }
  return std::format("{:.6g}", value);
}

std::string SysInfoString() {
#if defined(_WIN32)
  MEMORYSTATUSEX mem{};
  mem.dwLength = sizeof(mem);
  GlobalMemoryStatusEx(&mem);
  SYSTEM_INFO sys{};
  GetSystemInfo(&sys);

  const double total_gb = mem.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
  const double avail_gb = mem.ullAvailPhys / (1024.0 * 1024.0 * 1024.0);
  return std::format(
      "这台机器有 {} 个逻辑处理器, 内存共 {:.1f} GB, 当前可用 {:.1f} GB。",
      sys.dwNumberOfProcessors, total_gb, avail_gb);
#elif defined(__APPLE__)
  const long cpus = sysconf(_SC_NPROCESSORS_ONLN);
  uint64_t total_bytes = 0;
  size_t len = sizeof(total_bytes);
  sysctlbyname("hw.memsize", &total_bytes, &len, nullptr, 0);
  // macOS 没有直接的"可用内存"概念(vm_stat 更接近), 简单起见只报总量
  const double total_gb = total_bytes / (1024.0 * 1024.0 * 1024.0);
  return std::format("这台机器有 {} 个逻辑处理器, 内存共 {:.1f} GB。", cpus,
                     total_gb);
#else
  const long cpus = sysconf(_SC_NPROCESSORS_ONLN);
  struct sysinfo info{};
  const double total_gb =
      (sysinfo(&info) == 0)
          ? info.totalram * static_cast<double>(info.mem_unit) /
                (1024.0 * 1024.0 * 1024.0)
          : 0.0;
  const double avail_gb =
      (info.mem_unit > 0)
          ? info.freeram * static_cast<double>(info.mem_unit) /
                (1024.0 * 1024.0 * 1024.0)
          : 0.0;
  return std::format(
      "这台机器有 {} 个逻辑处理器, 内存共 {:.1f} GB, 当前空闲 {:.1f} GB。",
      cpus, total_gb, avail_gb);
#endif
}

}  // namespace

ToolRegistry::ToolRegistry() {
  tools_.push_back({
      .name = "time",
      .description = "查询当前日期时间",
      .keywords = {"几点", "什么时间", "现在时间", "今天几号", "今天星期",
                   "当前时间", "what time", "date today"},
      .handler = [](std::string_view) { return NowString(); },
  });

  tools_.push_back({
      .name = "calc",
      .description = "四则运算",
      .keywords = {"计算", "算一下", "等于多少", "算算"},
      .handler =
          [](std::string_view text) -> std::string {
            auto expr = ExtractExpression(text);
            if (!expr) return "我没看懂要算什么, 可以说比如「计算 12*(3+4)」";
            ExprEval eval(*expr);
            auto value = eval.Eval();
            if (!value) return std::format("算式「{}」我解不了, 换个简单点的?", *expr);
            return std::format("{} = {}", *expr, FormatNumber(*value));
          },
  });

  tools_.push_back({
      .name = "sysinfo",
      .description = "查询本机 CPU/内存概况",
      .keywords = {"系统信息", "电脑配置", "内存多少", "有几个核", "cpu",
                   "处理器"},
      .handler = [](std::string_view) { return SysInfoString(); },
  });
}

std::optional<ToolResult> ToolRegistry::TryHandle(
    std::string_view user_text) const {
  // 纯表达式也直接当计算: "12*(3+4)"
  if (auto expr = ExtractExpression(user_text); expr &&
      user_text.find_first_not_of("0123456789+-*/(). \t") ==
          std::string_view::npos) {
    ExprEval eval(*expr);
    if (auto value = eval.Eval()) {
      return ToolResult{"calc",
                        std::format("{} = {}", *expr, FormatNumber(*value))};
    }
  }

  for (const auto& tool : tools_) {
    for (const auto& keyword : tool.keywords) {
      if (ContainsIgnoreCase(user_text, keyword)) {
        return ToolResult{tool.name, tool.handler(user_text)};
      }
    }
  }
  return std::nullopt;
}

std::string ToolRegistry::PromptHint() const {
  std::ostringstream oss;
  oss << "你还可以引导用户使用这些本地工具(它们不走云端): ";
  for (size_t i = 0; i < tools_.size(); ++i) {
    if (i) oss << "; ";
    oss << tools_[i].name << "(" << tools_[i].description << ")";
  }
  oss << "。";
  return oss.str();
}

}  // namespace echo::dialog
