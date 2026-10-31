#include "llm_api.h"
#include <iostream>

int main() {
    LlmConfig cfg;
    cfg.api_base = "https://api.deepseek.com";
    cfg.api_key  = "sk-41fb2561297b4258be533dbfcfcf439e";
    cfg.model    = "deepseek-flash";
    cfg.request_timeout_s = 120;
    cfg.max_tokens  = 256;
    cfg.temperature = 0.7f;

    LlmClient llm(cfg);

    std::vector<ChatMessage> msgs = {
        ChatMessage::System("你是一个真实人类助手,请用中文回答问题。"),
        ChatMessage::User("你好"),
    };

    // 非流式
    // ChatResult r = llm.Chat(msgs);
    // if (!r.ok()) {
    //     std::cerr << r.error << "\n";
    // } else {
    //     std::cout << r.content << "\n";
    // }

    // 流式
    llm.ChatStream(msgs,
        [](const std::string& chunk, bool done, const std::string& err) {
            if (!err.empty()) { std::cerr << err; return false; }
            if (!done) std::cout << chunk;
            return true;      // 返回 false 可中途取消
        });
    std::cout << std::endl;
}
