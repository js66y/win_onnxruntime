#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

struct ChatMessage;

// =============================================================================
// ChatParams — request parameters passed to backend
// =============================================================================

struct ChatParams {
    int max_tokens = 512;
    float temperature = 0.7f;
    float top_p = 1.0f;
    std::vector<std::string> stop;
    std::string response_format;       // "" or "json_object"
    std::string tool_choice = "auto";
    float frequency_penalty = 0.0f;
    float presence_penalty = 0.0f;
    int seed = -1;                     // -1 means not set
    std::string tools_json;
};

// =============================================================================
// ChatResult — backend response
// =============================================================================

struct ChatResult {
    std::string content;
    std::string tool_calls_json;
    bool has_tool_calls = false;
    bool cancelled = false;
    std::string error;
    std::string finish_reason;
    int prompt_tokens = 0;
    int completion_tokens = 0;
    int total_tokens = 0;
};

// =============================================================================
// ILlmBackend — abstract backend interface
// =============================================================================

class ILlmBackend {
public:
    virtual ~ILlmBackend() = default;

    virtual bool initialize() = 0;
    virtual bool is_available() const = 0;
    virtual std::string get_backend_name() const = 0;

    using StreamCallback = std::function<bool(
        const std::string& chunk, bool is_done, const std::string& error)>;

    virtual ChatResult chat_stream(
        const std::vector<ChatMessage>& messages,
        const ChatParams& params,
        StreamCallback callback) = 0;

    virtual ChatResult chat(
        const std::vector<ChatMessage>& messages,
        const ChatParams& params) = 0;

    virtual void update_config(const std::string& key, const std::string& value) = 0;
};
