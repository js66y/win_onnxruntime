#include <windows.h>

#include <shellapi.h>  // ShellExecuteW(WIN32_LEAN_AND_MEAN 不包含它)

#include <atomic>
#include <csignal>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include "common/config.h"
#include "common/console.h"
#include "common/log.h"
#include "engine/asr/sherpa_asr.h"
#include "engine/audio_io.h"
#include "engine/llm/genai_llm.h"
#include "engine/tts/sherpa_tts.h"
#include "server/chat_service.h"
#include "server/http_server.h"

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

void PrintStats(const echo::engine::LlmTurnStats& stats) {
  std::cout << std::format(
      "\n\x1b[90m[提示 {} tok | 生成 {} tok | 首字 {:.2f}s | {:.1f} tok/s]"
      "\x1b[0m\n\n",
      stats.prompt_tokens, stats.new_tokens, stats.first_token_seconds,
      stats.TokensPerSecond());
}

// 交互式文字聊天(M1)
int RunInteractive(const echo::AppConfig& config) {
  std::cout << "正在加载模型 " << config.llm.model_dir.filename().string()
            << " ..." << std::flush;
  auto llm = echo::engine::GenAiLlm::Create(config.llm);
  if (!llm) {
    std::cout << "\n";
    echo::log::Error("{}", llm.error().message);
    return 1;
  }
  if (auto started = (*llm)->StartSession(config.system_prompt); !started) {
    std::cout << "\n";
    echo::log::Error("{}", started.error().message);
    return 1;
  }
  std::cout << " 完成\n\n";

  g_llm.store(llm->get());
  std::signal(SIGINT, OnInterrupt);

  std::cout << "\x1b[36m"
            << "==============================================\n"
            << "  Echo(回声) - 离线语音助手 [文字聊天]\n"
            << "==============================================\x1b[0m\n"
            << "命令: /reset 清空对话 | /exit 退出 | Ctrl+C 打断生成\n\n";

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
    PrintStats((*llm)->last_stats());
  }

  g_llm.store(nullptr);
  std::cout << "再见!\n";
  return 0;
}

// 语音文件模式(M2): wav 进 -> ASR -> LLM -> TTS -> wav 出
int RunVoiceFile(const echo::AppConfig& config,
                 const std::filesystem::path& input_wav,
                 const std::filesystem::path& output_wav) {
  using namespace echo::engine;

  auto audio = ReadWavFile(input_wav);
  if (!audio) {
    echo::log::Error("{}", audio.error().message);
    return 1;
  }
  std::cout << std::format("已读取 {} ({:.1f}s, {}Hz)\n", input_wav.string(),
                           audio->DurationSeconds(), audio->sample_rate);

  std::cout << "正在加载 ASR / LLM / TTS 模型..." << std::flush;
  auto asr = SherpaAsr::Create(config.asr);
  auto llm = GenAiLlm::Create(config.llm);
  auto tts = SherpaTts::Create(config.tts);
  for (const auto& message :
       {asr ? "" : asr.error().message.c_str(),
        llm ? "" : llm.error().message.c_str(),
        tts ? "" : tts.error().message.c_str()}) {
    if (*message != '\0') {
      std::cout << "\n";
      echo::log::Error("{}", message);
      return 1;
    }
  }
  std::cout << " 完成\n\n";

  // 1) 识别
  auto recognized = (*asr)->Recognize(audio->samples, audio->sample_rate);
  if (!recognized) {
    echo::log::Error("{}", recognized.error().message);
    return 1;
  }
  std::cout << "\x1b[32m你(语音)>\x1b[0m " << recognized->text;
  if (!recognized->lang.empty()) {
    std::cout << std::format(" \x1b[90m[{}]\x1b[0m", recognized->lang);
  }
  std::cout << "\n";

  // 2) 大模型回答
  if (auto started = (*llm)->StartSession(config.system_prompt); !started) {
    echo::log::Error("{}", started.error().message);
    return 1;
  }
  g_llm.store(llm->get());
  std::signal(SIGINT, OnInterrupt);

  std::string reply;
  std::cout << "\x1b[33m回声>\x1b[0m " << std::flush;
  for (const std::string& piece : (*llm)->Chat(recognized->text)) {
    std::cout << piece << std::flush;
    reply += piece;
  }
  PrintStats((*llm)->last_stats());
  g_llm.store(nullptr);

  if (reply.empty()) {
    echo::log::Error("模型没有生成任何回复");
    return 1;
  }

  // 3) 合成回复语音
  auto synthesized = (*tts)->Synthesize(reply);
  if (!synthesized) {
    echo::log::Error("{}", synthesized.error().message);
    return 1;
  }
  if (auto written = WriteWavFile(output_wav, *synthesized); !written) {
    echo::log::Error("{}", written.error().message);
    return 1;
  }
  std::cout << std::format("语音回复已保存: {} ({:.1f}s)\n",
                           output_wav.string(),
                           synthesized->DurationSeconds());
  return 0;
}

// Web 服务模式(M3/M4): 浏览器聊天界面 + 实时语音
int RunServer(const echo::AppConfig& config) {
  std::cout << "正在加载 LLM " << config.llm.model_dir.filename().string()
            << " ..." << std::flush;
  auto chat = echo::server::ChatService::Create(config);
  if (!chat) {
    std::cout << "\n";
    echo::log::Error("{}", chat.error().message);
    return 1;
  }
  std::cout << " 完成\n正在加载语音模型(VAD/ASR/TTS)..." << std::flush;

  // 语音模型缺失时不致命: 退化为纯文字聊天
  std::unique_ptr<echo::server::SpeechService> speech;
  if (auto created = echo::server::SpeechService::Create(config)) {
    speech = std::move(*created);
    std::cout << " 完成\n";
  } else {
    std::cout << "\n";
    echo::log::Warn("语音功能不可用: {}", created.error().message);
  }

  boost::asio::io_context ioc;
  if (auto started = echo::server::StartHttpServer(ioc, config, **chat,
                                                   speech.get());
      !started) {
    echo::log::Error("{}", started.error().message);
    return 1;
  }

  // Ctrl+C / 关闭信号: 停掉事件循环, ChatService 析构时会停推理线程
  boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
  signals.async_wait([&ioc](auto, auto) { ioc.stop(); });

  const auto url = std::format("http://{}:{}/", config.server.host,
                               config.server.port);
  std::cout << std::format(
                   "\x1b[36mEcho 服务已启动: {}\x1b[0m\n按 Ctrl+C 退出\n", url)
            << std::flush;  // stdout 重定向时也立即可见

  // 自动打开浏览器
  const std::wstring wide_url(url.begin(), url.end());
  ShellExecuteW(nullptr, L"open", wide_url.c_str(), nullptr, nullptr,
                SW_SHOWNORMAL);

  ioc.run();
  std::cout << "服务已停止\n";
  return 0;
}

void PrintUsage() {
  std::cout << "用法:\n"
            << "  echo.exe [config.json]              交互式文字聊天\n"
            << "  echo.exe --serve [--config config.json]\n"
            << "                                      Web 服务(浏览器聊天界面)\n"
            << "  echo.exe --voice <in.wav> [out.wav] [--config config.json]\n"
            << "                                      语音问答(wav 进 wav 出)\n";
}

}  // namespace

int main(int argc, char** argv) {
  echo::console::Init();

  // 解析命令行
  bool serve_mode = false;
  bool voice_mode = false;
  std::filesystem::path input_wav;
  std::filesystem::path output_wav = "reply.wav";
  std::filesystem::path config_file;
  std::vector<std::string> positional;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--serve") {
      serve_mode = true;
    } else if (arg == "--voice") {
      voice_mode = true;
    } else if (arg == "--config" && i + 1 < argc) {
      config_file = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      PrintUsage();
      return 0;
    } else {
      positional.emplace_back(arg);
    }
  }

  if (voice_mode) {
    if (positional.empty()) {
      PrintUsage();
      return 1;
    }
    input_wav = positional[0];
    if (positional.size() > 1) output_wav = positional[1];
  } else if (!positional.empty()) {
    config_file = positional[0];  // 兼容 M1 用法: echo.exe config.json
  }

  auto config = config_file.empty() ? echo::AppConfig::LoadDefault()
                                    : echo::AppConfig::LoadFile(config_file);
  if (!config) {
    echo::log::Error("加载配置失败: {}", config.error().message);
    return 1;
  }

  if (serve_mode) return RunServer(*config);
  return voice_mode ? RunVoiceFile(*config, input_wav, output_wav)
                    : RunInteractive(*config);
}
