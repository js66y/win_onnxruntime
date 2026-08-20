#include "engine/llm/genai_llm.h"

#include <chrono>
#include <filesystem>
#include <format>

#include "common/log.h"
#include "ort_genai.h"

namespace echo::engine {

struct GenAiLlm::Impl {
  LlmConfig config;
  std::unique_ptr<OgaModel> model;
  std::unique_ptr<OgaTokenizer> tokenizer;
  std::unique_ptr<OgaTokenizerStream> stream;
  std::unique_ptr<OgaGeneratorParams> params;
  std::unique_ptr<OgaGenerator> generator;
  size_t system_token_count = 0;
  LlmTurnStats stats;

  // 编码文本并追加进生成器的 KV cache
  Result<void> AppendText(const std::string& text) {
    try {
      auto sequences = OgaSequences::Create();
      tokenizer->Encode(text.c_str(), *sequences);
      generator->AppendTokenSequences(*sequences);
    } catch (const std::exception& e) {
      return Fail(ErrorCode::kGenerationFailed, "追加提示词失败: {}", e.what());
    }
    return {};
  }
};

GenAiLlm::GenAiLlm() : impl_(std::make_unique<Impl>()) {}
GenAiLlm::~GenAiLlm() = default;
GenAiLlm::GenAiLlm(GenAiLlm&&) noexcept = default;
GenAiLlm& GenAiLlm::operator=(GenAiLlm&&) noexcept = default;

Result<std::unique_ptr<GenAiLlm>> GenAiLlm::Create(LlmConfig config) {
  // OgaHandle 负责库的进程级初始化与关闭, 放 static 保证只有一份
  static OgaHandle oga_lifetime;

  if (!std::filesystem::exists(config.model_dir / "genai_config.json")) {
    return Fail(ErrorCode::kModelLoadFailed,
                "无效的模型目录(缺少 genai_config.json): {}",
                config.model_dir.string());
  }

  auto llm = std::unique_ptr<GenAiLlm>(new GenAiLlm());
  auto& impl = *llm->impl_;
  impl.config = std::move(config);
  try {
    const auto model_dir = impl.config.model_dir.string();
    impl.model = OgaModel::Create(model_dir.c_str());
    impl.tokenizer = OgaTokenizer::Create(*impl.model);
    impl.stream = OgaTokenizerStream::Create(*impl.tokenizer);
    impl.params = OgaGeneratorParams::Create(*impl.model);
    impl.params->SetSearchOption("max_length", impl.config.max_length);
    impl.params->SetSearchOptionBool("do_sample", impl.config.do_sample);
    impl.params->SetSearchOption("temperature", impl.config.temperature);
    impl.params->SetSearchOption("top_p", impl.config.top_p);
    impl.generator = OgaGenerator::Create(*impl.model, *impl.params);
  } catch (const std::exception& e) {
    return Fail(ErrorCode::kModelLoadFailed, "加载模型失败: {}", e.what());
  }
  return llm;
}

Result<void> GenAiLlm::StartSession(std::string_view system_prompt) {
  // Qwen 系列使用 ChatML 对话格式
  const auto prompt =
      std::format("<|im_start|>system\n{}<|im_end|>\n", system_prompt);
  if (auto appended = impl_->AppendText(prompt); !appended) return appended;
  impl_->system_token_count = impl_->generator->TokenCount();
  return {};
}

void GenAiLlm::Chat(std::string user_text, const OnPiece& on_piece) {
  auto& impl = *impl_;
  impl.stats = {};

  // 复位可能挂起的终止标志: RequestStop() 可能在空闲时被调用
  try {
    impl.generator->SetRuntimeOption("terminate_session", "0");
  } catch (const std::exception& e) {
    log::Warn("复位终止标志失败: {}", e.what());
  }

  auto prompt = std::format(
      "<|im_start|>user\n{}<|im_end|>\n<|im_start|>assistant\n", user_text);
  if (impl.config.disable_thinking) {
    // 与 Qwen3 enable_thinking=false 的官方模板一致: 直接补一个空思考块
    prompt += "<think>\n\n</think>\n\n";
  }

  const size_t tokens_before_turn = impl.generator->TokenCount();
  if (auto appended = impl.AppendText(prompt); !appended) {
    log::Error("{}", appended.error().message);
    return;
  }
  impl.stats.prompt_tokens =
      static_cast<int>(impl.generator->TokenCount() - tokens_before_turn);

  const auto start = std::chrono::steady_clock::now();
  const auto elapsed = [&start] {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                         start)
        .count();
  };

  bool first_token = true;
  while (true) {
    std::string piece;
    try {
      if (impl.generator->IsDone()) break;
      impl.generator->GenerateNextToken();
      if (first_token) {
        impl.stats.first_token_seconds = elapsed();
        first_token = false;
      }
      piece = impl.stream->Decode(impl.generator->GetNextTokens()[0]);
    } catch (const std::exception& e) {
      // 两种情况会走到这里: RequestStop() 触发的主动终止, 或后端错误。
      // 统一处理: 复位终止标志, 回退到本轮之前的状态。
      log::Debug("生成中止: {}", e.what());
      try {
        impl.generator->SetRuntimeOption("terminate_session", "0");
        impl.generator->RewindTo(tokens_before_turn);
      } catch (const std::exception& rewind_error) {
        log::Warn("回退会话状态失败: {}", rewind_error.what());
      }
      break;
    }
    impl.stats.new_tokens += 1;
    if (on_piece) on_piece(std::move(piece));
  }
  impl.stats.total_seconds = elapsed();
}

void GenAiLlm::RequestStop() {
  try {
    impl_->generator->SetRuntimeOption("terminate_session", "1");
  } catch (...) {
    // 信号处理器上下文中不做任何进一步处理
  }
}

Result<void> GenAiLlm::ResetSession() {
  try {
    impl_->generator->RewindTo(impl_->system_token_count);
  } catch (const std::exception& e) {
    return Fail(ErrorCode::kGenerationFailed, "清空对话失败: {}", e.what());
  }
  return {};
}

Result<void> GenAiLlm::RestartSession(std::string_view system_prompt) {
  try {
    impl_->generator->SetRuntimeOption("terminate_session", "0");
    impl_->generator = OgaGenerator::Create(*impl_->model, *impl_->params);
    impl_->system_token_count = 0;
  } catch (const std::exception& e) {
    return Fail(ErrorCode::kGenerationFailed, "重建会话失败: {}", e.what());
  }
  return StartSession(system_prompt);
}

const LlmTurnStats& GenAiLlm::last_stats() const { return impl_->stats; }

}  // namespace echo::engine
