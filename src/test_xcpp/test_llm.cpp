#include "llm_api.h"          // ChatMessage 的定义在这里
#include "openai_backend.h"
#include <iostream>
using namespace std;

int main(){
    OpenAIBackend::Config cfg;
    cfg.api_base = "https://api.deepseek.com";   // 不带结尾 /
    cfg.api_key  = "sk-41fb2561297b4258be533dbfcfcf439e";
    cfg.model    = "deepseek-flash";
    cfg.request_timeout_s = 120;

    OpenAIBackend backend(cfg);
    backend.initialize();

    ChatParams p;
    p.max_tokens  = 256;
    p.temperature = 0.7f;

    std::vector<ChatMessage> msgs = {
        ChatMessage::System("你是一个助手"),
        ChatMessage::User("你好"),
    };

    // 非流式
    ChatResult r = backend.chat(msgs, p);
    if (!r.error.empty()) { /* 失败 */ }
    std::cout << r.content;

    // 流式
    backend.chat_stream(msgs, p,
        [](const std::string& chunk, bool done, const std::string& err) {
            if (!err.empty()) { /* 错误 */ return false; }
            if (!done) std::cout << chunk;
            return true;      // 返回 false 可中途取消
        });

}
