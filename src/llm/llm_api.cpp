#include "llm_api.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <mutex>
#include <stdexcept>

LlmClient::LlmClient(const LlmConfig& config) : config(config) {}

ChatResult LlmClient::Chat(const std::vector<ChatMessage>& messages) {
    ChatResult result;
    try {
        std::string response = http_post(config, build_request(config, messages, false));
        auto j = nlohmann::json::parse(response);

        if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
            auto& choice = j["choices"][0];
            if (choice.contains("message") && choice["message"].contains("content") &&
                !choice["message"]["content"].is_null()) {
                result.content = choice["message"]["content"].get<std::string>();
            }
            if (choice.contains("finish_reason") && !choice["finish_reason"].is_null()) {
                result.finish_reason = choice["finish_reason"].get<std::string>();
            }
        }
    } catch (const std::exception& e) {
        result.error = e.what();
    }
    return result;
}

ChatResult LlmClient::ChatStream(const std::vector<ChatMessage>& messages, StreamCallback callback) {
    ChatResult result;
    StreamState st;
    st.callback = std::move(callback);

    http_post_stream(config, build_request(config, messages, true), st);

    result.content = std::move(st.content);
    result.finish_reason = std::move(st.finish_reason);
    result.error = std::move(st.error);
    result.cancelled = st.cancelled;
    return result;
}


void ensure_curl() {
    static std::once_flag flag;
    std::call_once(flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* data) {
    data->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}


void emit_done(StreamState& st) {
    if (st.done) return;
    st.done = true;
    st.callback("", true, "");
}

void emit_error(StreamState& st, const std::string& error) {
    if (st.done) return;
    st.done = true;
    st.error = error;
    st.callback("", true, error);
}

size_t StreamWriteCallback(void* contents, size_t size, size_t nmemb, StreamState* st) {
    size_t total_size = size * nmemb;
    if (st->cancelled || st->done) return total_size;

    st->buffer.append(static_cast<const char*>(contents), total_size);

    size_t pos = 0;
    while (pos < st->buffer.length()) {
        size_t data_pos = st->buffer.find("data: ", pos);
        if (data_pos == std::string::npos) {
            st->buffer = st->buffer.substr(pos);
            break;
        }

        size_t line_end = st->buffer.find('\n', data_pos);
        if (line_end == std::string::npos) {
            st->buffer = st->buffer.substr(data_pos);
            break;
        }

        std::string data_line = st->buffer.substr(data_pos + 6, line_end - data_pos - 6);
        if (!data_line.empty() && data_line.back() == '\r') data_line.pop_back();
        pos = line_end + 1;

        if (data_line == "[DONE]") {
            emit_done(*st);
            st->buffer.clear();
            return total_size;
        }
        if (data_line.empty()) continue;

        try {
            auto j = nlohmann::json::parse(data_line);

            if (j.contains("error")) {
                std::string err = j["error"].contains("message")
                    ? j["error"]["message"].get<std::string>()
                    : "Unknown API error";
                emit_error(*st, err);
                st->buffer.clear();
                return total_size;
            }

            if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty()) {
                continue;
            }

            auto& choice = j["choices"][0];

            if (choice.contains("delta") && choice["delta"].contains("content") &&
                !choice["delta"]["content"].is_null()) {
                std::string chunk = choice["delta"]["content"].get<std::string>();
                if (!chunk.empty()) {
                    st->content += chunk;
                    if (!st->callback(chunk, false, "")) {
                        st->cancelled = true;
                        return 0;  // 中止传输
                    }
                }
            }

            if (choice.contains("finish_reason") && !choice["finish_reason"].is_null()) {
                st->finish_reason = choice["finish_reason"].get<std::string>();
                emit_done(*st);
            }
        } catch (const nlohmann::json::exception&) {
            // 跳过无法解析的分片
        }
    }

    return total_size;
}

std::string build_request(const LlmConfig& config,
                          const std::vector<ChatMessage>& messages,
                          bool stream) {
    nlohmann::json msgs = nlohmann::json::array();
    for (const auto& m : messages) {
        msgs.push_back({{"role", m.role}, {"content", m.content}});
    }

    nlohmann::json request;
    request["model"] = config.model;
    request["messages"] = std::move(msgs);
    request["max_tokens"] = config.max_tokens;
    request["temperature"] = config.temperature;
    request["stream"] = stream;
    return request.dump();
}

struct curl_slist* make_headers(const std::string& api_key, bool stream) {
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (stream) headers = curl_slist_append(headers, "Accept: text/event-stream");
    if (!api_key.empty()) {
        std::string auth = "Authorization: Bearer " + api_key;
        headers = curl_slist_append(headers, auth.c_str());
    }
    return headers;
}

CURL* make_curl(const LlmConfig& config, const std::string& body, struct curl_slist* headers) {
    std::string url = config.api_base + "/chat/completions";
    CURL* curl = curl_easy_init();
    if (!curl) return nullptr;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, static_cast<long>(config.connect_timeout_s));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(config.request_timeout_s));
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
    return curl;
}

std::string http_post(const LlmConfig& config, const std::string& body) {
    ensure_curl();

    std::string response;
    struct curl_slist* headers = make_headers(config.api_key, false);
    CURL* curl = make_curl(config, body, headers);
    if (!curl) {
        curl_slist_free_all(headers);
        throw std::runtime_error("Failed to initialize CURL");
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        throw std::runtime_error("CURL error: " + std::string(curl_easy_strerror(res)));
    }
    if (code != 200) {
        throw std::runtime_error("HTTP " + std::to_string(code) + ": " + response);
    }
    return response;
}

void http_post_stream(const LlmConfig& config, const std::string& body, StreamState& st) {
    ensure_curl();

    struct curl_slist* headers = make_headers(config.api_key, true);
    CURL* curl = make_curl(config, body, headers);
    if (!curl) {
        curl_slist_free_all(headers);
        emit_error(st, "Failed to initialize CURL");
        return;
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StreamWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &st);

    CURLcode res = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (st.cancelled || st.done) return;
    if (res != CURLE_OK) {
        emit_error(st, "CURL error: " + std::string(curl_easy_strerror(res)));
    } else if (code != 200) {
        emit_error(st, "HTTP error: " + std::to_string(code));
    } else {
        emit_done(st);
    }
}

