#include <atomic>
#include <csignal>
#include <format>
#include <iostream>
#include <string>

#include "common/config.h"
#include "common/console.h"
#include "common/log.h"
#include "engine/llm/genai_llm.h"

namespace {

std::atomic<echo::engine::GenAiLlm*> g_llm{nullptr};

// Ctrl+C: 只打断当前生成, 不退出程序(在独立线程中执行, 只做线程安全操作)
void OnInterrupt(int /*signal*/) {
  if (auto* llm = g_llm.load()) llm->RequestStop();
}

std::string Trim(std::string_view text) {
  constexpr std::string_view kSpaces = " \t\r\n";
  const auto begin = text.find_first_not_of(kSpaces);
  if (begin == std::string_view::npos) return {};
  const auto end = text.find_last_not_of(kSpaces);
  return std::string(text.substr(begin, end - begin + 1));
}

void PrintBanner(const echo::AppConfig& config) {
  std::cout << "\x1b[36m"
            << "==============================================\n"
            << "  Echo(回声) - 离线语音助手 [M1 文字聊天]\n"
            << "==============================================\x1b[0m\n"
            << "模型: " << config.llm.model_dir.filename().string() << "\n"
            << "命令: /reset 清空对话 | /exit 退出 | Ctrl+C 打断生成\n\n";
}

}  // namespace

int main(int argc, char** argv) {
  echo::console::Init();

  auto config = (argc > 1) ? echo::AppConfig::LoadFile(argv[1])
                           : echo::AppConfig::LoadDefault();
  if (!config) {
    echo::log::Error("加载配置失败: {}", config.error().message);
    return 1;
  }

  std::cout << "正在加载模型 " << config->llm.model_dir.filename().string()
            << " ..." << std::flush;
  auto llm = echo::engine::GenAiLlm::Create(config->llm);
  if (!llm) {
    std::cout << "\n";
    echo::log::Error("{}", llm.error().message);
    return 1;
  }
  if (auto started = (*llm)->StartSession(config->system_prompt); !started) {
    std::cout << "\n";
    echo::log::Error("{}", started.error().message);
    return 1;
  }
  std::cout << " 完成\n\n";

  g_llm.store(llm->get());
  std::signal(SIGINT, OnInterrupt);

  PrintBanner(*config);

  while (true) {
    std::cout << "\x1b[32m你>\x1b[0m " << std::flush;
    const auto raw = echo::console::ReadLineUtf8();
    if (!raw) break;  // EOF (Ctrl+Z 或管道结束)

    const auto line = Trim(*raw);
    if (line.empty()) continue;
    if (line == "/exit" || line == "/quit") break;
    if (line == "/reset") {
      if (auto reset = (*llm)->ResetSession(); !reset) {
        echo::log::Error("{}", reset.error().message);
      } else {
        std::cout << "(对话已清空)\n";
      }
      continue;
    }

    std::cout << "\x1b[33m回声>\x1b[0m " << std::flush;
    for (const std::string& piece : (*llm)->Chat(line)) {
      std::cout << piece << std::flush;
    }

    const auto& stats = (*llm)->last_stats();
    std::cout << std::format(
        "\n\x1b[90m[提示 {} tok | 生成 {} tok | 首字 {:.2f}s | {:.1f} tok/s]"
        "\x1b[0m\n\n",
        stats.prompt_tokens, stats.new_tokens, stats.first_token_seconds,
        stats.TokensPerSecond());
  }

  g_llm.store(nullptr);
  std::cout << "再见!\n";
  return 0;
}
