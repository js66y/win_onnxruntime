#include "openai_backend.h"
#include "llm_api.h"
#include <stdexcept>
#include <map>
#include <mutex>

#ifdef USE_HTTP_CLIENT
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#endif

#ifdef USE_HTTP_CLIENT

// =============================================================================
// HTTP helpers
// =============================================================================

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* data) {
    data->append((char*)contents, size * nmemb);
    return size * nmemb;
}

struct StreamData {
    std::function<bool(const std::string& chunk, bool is_done, const std::string& error)> callback;
    std::string buffer;
    std::string full_content;
    bool cancelled = false;
    std::string finish_reason;

    // Tool calls accumulator (streamed in fragments)
    std::map<int, nlohmann::json> tool_call_map;
    bool has_tool_calls = false;
};

static size_t StreamWriteCallback(void* contents, size_t size, size_t nmemb, StreamData* sd) {
    if (sd->cancelled) return 0;

    size_t total_size = size * nmemb;
    sd->buffer.append((const char*)contents, total_size);

    size_t pos = 0;
    while (pos < sd->buffer.length()) {
        size_t data_pos = sd->buffer.find("data: ", pos);
        if (data_pos == std::string::npos) {
            sd->buffer = sd->buffer.substr(pos);
            break;
        }

        size_t line_end = sd->buffer.find('\n', data_pos);
        if (line_end == std::string::npos) {
            sd->buffer = sd->buffer.substr(data_pos);
            break;
        }

        std::string data_line = sd->buffer.substr(data_pos + 6, line_end - data_pos - 6);
        if (!data_line.empty() && data_line.back() == '\r') data_line.pop_back();

        pos = line_end + 1;

        if (data_line == "[DONE]") {
            sd->callback("", true, "");
            sd->buffer.clear();
            return total_size;
        }

        if (data_line.empty()) continue;

        try {
            auto j = nlohmann::json::parse(data_line);

            if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
                auto& choice = j["choices"][0];

                if (choice.contains("delta")) {
                    auto& delta = choice["delta"];

                    // Content token
                    if (delta.contains("content") && !delta["content"].is_null()) {
                        std::string content = delta["content"].get<std::string>();
                        if (!content.empty()) {
                            sd->full_content += content;
                            bool ok = sd->callback(content, false, "");
                            if (!ok) {
                                sd->cancelled = true;
                                return 0;  // Abort curl transfer
                            }
                        }
                    }

                    // Tool calls (streamed in fragments)
                    if (delta.contains("tool_calls")) {
                        sd->has_tool_calls = true;
                        for (const auto& tc : delta["tool_calls"]) {
                            int idx = tc.value("index", 0);

                            if (tc.contains("id")) {
                                sd->tool_call_map[idx] = {
                                    {"id", tc["id"]},
                                    {"type", "function"},
                                    {"function", {{"name", ""}, {"arguments", ""}}}
                                };
                            }

                            if (tc.contains("function")) {
                                auto& entry = sd->tool_call_map[idx];
                                if (tc["function"].contains("name")) {
                                    entry["function"]["name"] =
                                        entry["function"]["name"].get<std::string>() +
                                        tc["function"]["name"].get<std::string>();
                                }
                                if (tc["function"].contains("arguments")) {
                                    entry["function"]["arguments"] =
                                        entry["function"]["arguments"].get<std::string>() +
                                        tc["function"]["arguments"].get<std::string>();
                                }
                            }
                        }
                    }
                }

                // Check finish_reason
                if (choice.contains("finish_reason") && !choice["finish_reason"].is_null()) {
                    sd->finish_reason = choice["finish_reason"].get<std::string>();
                    sd->callback("", true, "");
                    sd->buffer.clear();
                    return total_size;
                }
            }

            if (j.contains("error")) {
                std::string err = j["error"].contains("message")
                    ? j["error"]["message"].get<std::string>()
                    : "Unknown API error";
                sd->callback("", true, err);
                sd->buffer.clear();
                return total_size;
            }
        } catch (const nlohmann::json::exception& e) {
            // Log and skip malformed JSON chunks
            (void)e;
        }
    }

    return total_size;
}

static std::string http_post(const std::string& url,
                             const std::string& json_data,
                             const std::string& api_key,
                             int connect_timeout,
                             int request_timeout) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("Failed to initialize CURL");

    std::string response_data;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!api_key.empty()) {
        std::string auth = "Authorization: Bearer " + api_key;
        headers = curl_slist_append(headers, auth.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)connect_timeout);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)request_timeout);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        curl_easy_cleanup(curl);
        throw std::runtime_error("CURL error: " + std::string(curl_easy_strerror(res)));
    }

    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);

    if (code != 200) {
        throw std::runtime_error("HTTP " + std::to_string(code) + ": " + response_data);
    }
    return response_data;
}

static void http_post_stream(const std::string& url,
                             const std::string& json_data,
                             const std::string& api_key,
                             int connect_timeout,
                             int request_timeout,
                             StreamData& sd) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        sd.callback("", true, "Failed to initialize CURL");
        return;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: text/event-stream");
    if (!api_key.empty()) {
        std::string auth = "Authorization: Bearer " + api_key;
        headers = curl_slist_append(headers, auth.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StreamWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sd);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)connect_timeout);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)request_timeout);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK && !sd.cancelled) {
        curl_easy_cleanup(curl);
        sd.callback("", true, "CURL error: " + std::string(curl_easy_strerror(res)));
        return;
    }

    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);

    if (code != 200 && !sd.cancelled) {
        sd.callback("", true, "HTTP error: " + std::to_string(code));
    }
}

static nlohmann::json build_messages_json(const std::vector<ChatMessage>& messages) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& m : messages) {
        nlohmann::json msg = {{"role", m.role}, {"content", m.content}};
        if (!m.tool_calls_json.empty()) {
            msg["tool_calls"] = nlohmann::json::parse(m.tool_calls_json);
        }
        if (!m.tool_call_id.empty()) {
            msg["tool_call_id"] = m.tool_call_id;
        }
        arr.push_back(msg);
    }
    return arr;
}

static nlohmann::json build_request_json(const std::string& model,
                                         const std::vector<ChatMessage>& messages,
                                         const ChatParams& params,
                                         bool stream) {
    nlohmann::json request;
    request["model"] = model;
    request["max_tokens"] = params.max_tokens;
    request["temperature"] = params.temperature;
    request["stream"] = stream;
    request["messages"] = build_messages_json(messages);

    if (params.top_p != 1.0f) request["top_p"] = params.top_p;
    if (!params.stop.empty()) {
        request["stop"] = nlohmann::json::array();
        for (const auto& s : params.stop) request["stop"].push_back(s);
    }
    if (!params.response_format.empty()) {
        request["response_format"] = {{"type", params.response_format}};
    }
    if (params.frequency_penalty != 0.0f) request["frequency_penalty"] = params.frequency_penalty;
    if (params.presence_penalty != 0.0f) request["presence_penalty"] = params.presence_penalty;
    if (params.seed >= 0) request["seed"] = params.seed;

    if (!params.tools_json.empty()) {
        try {
            request["tools"] = nlohmann::json::parse(params.tools_json);
            request["tool_choice"] = params.tool_choice;
        } catch (const nlohmann::json::exception& e) {
            // tools_json parse failed — skip tools, error logged in result
            (void)e;
        }
    }

    return request;
}

#endif  // USE_HTTP_CLIENT

// =============================================================================
// OpenAIBackend implementation
// =============================================================================

struct OpenAIBackend::Impl {
    std::string model;
    std::string api_base;
    std::string api_key;
    int connect_timeout_s = 30;
    int request_timeout_s = 300;
    bool initialized = false;
};

OpenAIBackend::OpenAIBackend(const Config& config) : pimpl_(std::make_unique<Impl>()) {
    pimpl_->model = config.model;
    pimpl_->api_base = config.api_base;
    pimpl_->api_key = config.api_key;
    pimpl_->connect_timeout_s = config.connect_timeout_s;
    pimpl_->request_timeout_s = config.request_timeout_s;
}

OpenAIBackend::~OpenAIBackend() = default;

bool OpenAIBackend::initialize() {
#ifdef USE_HTTP_CLIENT
    static std::once_flag curl_init_flag;
    std::call_once(curl_init_flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    pimpl_->initialized = true;
    return true;
#else
    return false;
#endif
}

bool OpenAIBackend::is_available() const {
#ifdef USE_HTTP_CLIENT
    return true;
#else
    return false;
#endif
}

ChatResult OpenAIBackend::chat_stream(
    const std::vector<ChatMessage>& messages,
    const ChatParams& params,
    StreamCallback callback
) {
    ChatResult result;
#ifdef USE_HTTP_CLIENT
    if (!pimpl_->initialized) initialize();

    auto request = build_request_json(pimpl_->model, messages, params, true);
    std::string url = pimpl_->api_base + "/chat/completions";

    StreamData sd;
    sd.callback = callback;

    http_post_stream(url, request.dump(), pimpl_->api_key,
                     pimpl_->connect_timeout_s, pimpl_->request_timeout_s, sd);

    result.content = sd.full_content;
    result.cancelled = sd.cancelled;
    result.finish_reason = sd.finish_reason;

    if (sd.has_tool_calls && !sd.tool_call_map.empty()) {
        nlohmann::json tc_arr = nlohmann::json::array();
        for (const auto& [idx, tc] : sd.tool_call_map) {
            tc_arr.push_back(tc);
        }
        result.tool_calls_json = tc_arr.dump();
        result.has_tool_calls = true;
    }
#else
    result.error = "HTTP client not compiled in";
    callback("", true, result.error);
#endif
    return result;
}

ChatResult OpenAIBackend::chat(
    const std::vector<ChatMessage>& messages,
    const ChatParams& params
) {
    ChatResult result;
#ifdef USE_HTTP_CLIENT
    if (!pimpl_->initialized) initialize();

    auto request = build_request_json(pimpl_->model, messages, params, false);
    std::string url = pimpl_->api_base + "/chat/completions";

    try {
        std::string response = http_post(url, request.dump(), pimpl_->api_key,
                                         pimpl_->connect_timeout_s, pimpl_->request_timeout_s);
        auto j = nlohmann::json::parse(response);

        if (j.contains("choices") && !j["choices"].empty()) {
            auto& choice = j["choices"][0];
            auto& msg = choice["message"];
            if (msg.contains("content") && !msg["content"].is_null()) {
                result.content = msg["content"].get<std::string>();
            }
            if (msg.contains("tool_calls") && !msg["tool_calls"].empty()) {
                result.tool_calls_json = msg["tool_calls"].dump();
                result.has_tool_calls = true;
            }
            if (choice.contains("finish_reason") && !choice["finish_reason"].is_null()) {
                result.finish_reason = choice["finish_reason"].get<std::string>();
            }
        }

        if (j.contains("usage")) {
            auto& usage = j["usage"];
            if (usage.contains("prompt_tokens"))
                result.prompt_tokens = usage["prompt_tokens"].get<int>();
            if (usage.contains("completion_tokens"))
                result.completion_tokens = usage["completion_tokens"].get<int>();
            if (usage.contains("total_tokens"))
                result.total_tokens = usage["total_tokens"].get<int>();
        }
    } catch (const nlohmann::json::exception& e) {
        result.error = std::string("JSON parse error: ") + e.what();
    } catch (const std::exception& e) {
        result.error = e.what();
    }
#else
    result.error = "HTTP client not compiled in";
#endif
    return result;
}

void OpenAIBackend::update_config(const std::string& key, const std::string& value) {
    if (key == "api_base") pimpl_->api_base = value;
    else if (key == "api_key") pimpl_->api_key = value;
    else if (key == "model") pimpl_->model = value;
}
