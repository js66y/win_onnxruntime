#pragma once

#include <functional>
#include <string>
#include <vector>

// openai config
struct LlmConfig {
    std::string api_base = "https://api.deepseek.com";
    std::string api_key = "sk-41fb2561297b4258be533dbfcfcf439e";
    std::string model = "deepseek-flash";
    int max_tokens = 512;
    float temperature = 0.7f;
    int connect_timeout_s = 30;
    int request_timeout_s = 300;
};

// message
struct ChatMessage {
    std::string role;
    std::string content;

    static ChatMessage System(const std::string& content) { return {"system", content}; }
    static ChatMessage User(const std::string& content) { return {"user", content}; }
    static ChatMessage Assistant(const std::string& content) { return {"assistant", content}; }
};

// result
struct ChatResult {
    std::string content;
    std::string finish_reason;
    std::string error;
    bool cancelled = false;
    bool ok() const { return error.empty() && !cancelled; }
};
// state
struct StreamState {
    StreamCallback callback;
    std::string buffer;
    std::string content;
    std::string finish_reason;
    std::string error;
    bool cancelled = false;
    bool done = false;
};

// callback
using StreamCallback = std::function<bool(
    const std::string& chunk, bool is_done, const std::string& error)>;

class LlmClient {
public:
    explicit LlmClient(const LlmConfig& config);
    ~LlmClient() = default;

    LlmClient(const LlmClient&) = delete;
    LlmClient& operator=(const LlmClient&) = delete;

    ChatResult Chat(const std::vector<ChatMessage>& messages);
    ChatResult ChatStream(const std::vector<ChatMessage>& messages, StreamCallback callback);
private:
    LlmConfig config;
};
