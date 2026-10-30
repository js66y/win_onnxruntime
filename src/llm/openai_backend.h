#pragma once

#include "llm_backend.h"
#include <string>
#include <vector>
#include <memory>


class OpenAIBackend : public ILlmBackend {
public:
    struct Config {
        std::string model;
        std::string api_base;
        std::string api_key;
        int connect_timeout_s = 30;
        int request_timeout_s = 300;
    };

    explicit OpenAIBackend(const Config& config);
    ~OpenAIBackend() override;

    bool initialize() override;
    bool is_available() const override;
    std::string get_backend_name() const override { return "OpenAI API"; }

    ChatResult chat_stream(
        const std::vector<ChatMessage>& messages,
        const ChatParams& params,
        StreamCallback callback) override;

    ChatResult chat(
        const std::vector<ChatMessage>& messages,
        const ChatParams& params) override;

    void update_config(const std::string& key, const std::string& value) override;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};