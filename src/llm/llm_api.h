#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>

// =============================================================================
// LlmConfig
// =============================================================================

struct LlmConfig {
    std::string api_base = "http://localhost:11434/v1";
    std::string api_key;
    std::string model = "qwen2.5:0.5b";
    std::string system_prompt;
    int max_tokens = 512;
    float temperature = 0.7f;
    float top_p = 1.0f;
    std::vector<std::string> stop;
    std::string response_format;       // "" or "json_object"
    std::string tool_choice = "auto";
    float frequency_penalty = 0.0f;
    float presence_penalty = 0.0f;
    int seed = -1;                     // -1 means not set
    int connect_timeout_s = 30;
    int request_timeout_s = 300;

    static LlmConfig Default() { return LlmConfig(); }

    static LlmConfig Ollama(const std::string& model = "qwen2.5:0.5b",
                            const std::string& url = "http://localhost:11434/v1") {
        LlmConfig c;
        c.api_base = url;
        c.model = model;
        return c;
    }

    static LlmConfig CloudAPI(const std::string& api_base,
                              const std::string& api_key,
                              const std::string& model) {
        LlmConfig c;
        c.api_base = api_base;
        c.api_key = api_key;
        c.model = model;
        return c;
    }

    LlmConfig withModel(const std::string& m) const { auto c = *this; c.model = m; return c; }
    LlmConfig withMaxTokens(int n) const { auto c = *this; c.max_tokens = n; return c; }
    LlmConfig withTemperature(float t) const { auto c = *this; c.temperature = t; return c; }
    LlmConfig withSystemPrompt(const std::string& p) const { auto c = *this; c.system_prompt = p; return c; }
    LlmConfig withApiKey(const std::string& k) const { auto c = *this; c.api_key = k; return c; }
    LlmConfig withTimeout(int connect_s, int request_s) const {
        auto c = *this; c.connect_timeout_s = connect_s; c.request_timeout_s = request_s; return c;
    }
    LlmConfig withTopP(float p) const { auto c = *this; c.top_p = p; return c; }
    LlmConfig withStop(std::vector<std::string> s) const { auto c = *this; c.stop = std::move(s); return c; }
    LlmConfig withResponseFormat(const std::string& fmt) const { auto c = *this; c.response_format = fmt; return c; }
    LlmConfig withToolChoice(const std::string& tc) const { auto c = *this; c.tool_choice = tc; return c; }
    LlmConfig withFrequencyPenalty(float fp) const { auto c = *this; c.frequency_penalty = fp; return c; }
    LlmConfig withPresencePenalty(float pp) const { auto c = *this; c.presence_penalty = pp; return c; }
    LlmConfig withSeed(int s) const { auto c = *this; c.seed = s; return c; }
};

// =============================================================================
// ChatMessage
// =============================================================================

struct ChatMessage {
    std::string role;
    std::string content;
    std::string tool_calls_json;   // JSON string for assistant tool_calls
    std::string tool_call_id;      // for tool response messages

    static ChatMessage System(const std::string& content) {
        return {"system", content, "", ""};
    }
    static ChatMessage User(const std::string& content) {
        return {"user", content, "", ""};
    }
    static ChatMessage Assistant(const std::string& content,
                                 const std::string& tool_calls_json = "") {
        return {"assistant", content, tool_calls_json, ""};
    }
    static ChatMessage Tool(const std::string& content,
                            const std::string& tool_call_id) {
        return {"tool", content, "", tool_call_id};
    }
};

// =============================================================================
// LlmStreamCallback — returns false to cancel the stream
// =============================================================================

using LlmStreamCallback = std::function<bool(
    const std::string& chunk,
    bool is_done,
    const std::string& error
)>;

// =============================================================================
// LlmResult
// =============================================================================

struct LlmResult {
    std::string content;
    std::string tool_calls_json;
    bool has_tool_calls = false;
    bool cancelled = false;
    std::string error;
    double latency_ms = 0.0;
    std::string finish_reason;      // "stop", "length", "tool_calls", etc.
    int prompt_tokens = 0;
    int completion_tokens = 0;
    int total_tokens = 0;

    bool IsSuccess() const { return error.empty() && !cancelled; }
    bool HasToolCalls() const { return has_tool_calls; }
};

// =============================================================================
// LlmEngine
// =============================================================================

class LlmEngine {
public:
    explicit LlmEngine(const LlmConfig& config);
    explicit LlmEngine(const std::string& model = "qwen2.5:0.5b",
                       const std::string& api_base = "http://localhost:11434/v1");
    ~LlmEngine();

    // Non-copyable, movable
    LlmEngine(const LlmEngine&) = delete;
    LlmEngine& operator=(const LlmEngine&) = delete;
    LlmEngine(LlmEngine&&) = default;
    LlmEngine& operator=(LlmEngine&&) = default;

    // =========================================================================
    // Streaming generation
    // =========================================================================

    /// Chat completion with messages array (supports tools for MCP)
    LlmResult ChatStream(
        const std::vector<ChatMessage>& messages,
        LlmStreamCallback callback,
        const std::string& tools_json = ""
    );

    /// Simple prompt generation (wraps as [system, user] messages)
    LlmResult GenerateStream(
        const std::string& prompt,
        LlmStreamCallback callback
    );

    // =========================================================================
    // Synchronous generation
    // =========================================================================

    LlmResult Chat(const std::vector<ChatMessage>& messages,
                   const std::string& tools_json = "");

    std::string Generate(const std::string& prompt);

    // =========================================================================
    // Ollama management (works when api_base points to Ollama)
    // =========================================================================

    bool IsAvailable() const;
    std::string GetVersion() const;
    std::vector<std::string> ListModels() const;
    bool PullModel(const std::string& model_name) const;

    // =========================================================================
    // Dynamic configuration
    // =========================================================================

    void SetModel(const std::string& model);
    void SetSystemPrompt(const std::string& prompt);
    void SetMaxTokens(int max_tokens);
    LlmConfig GetConfig() const;
    bool IsInitialized() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
