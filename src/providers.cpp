#include "providers.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "grok_oauth.h"

namespace provider {

// --- config resolution ------------------------------------------------------

Provider make(const Config& c, const std::string& type, const std::string& model) {
    Provider p;
    p.model = model;
    if (type == "openai") {
        p.kind = Kind::OpenAI;
        p.name = "OpenAI";
        p.base_url = "https://api.openai.com";
        p.api_key = c.openai_key;
    } else if (type == "anthropic") {
        p.kind = Kind::Anthropic;
        p.name = "Anthropic";
        p.base_url = "https://api.anthropic.com";
        p.api_key = c.anthropic_key;
    } else if (type == "gemini") {
        p.kind = Kind::Gemini;
        p.name = "Gemini";
        p.base_url = "https://generativelanguage.googleapis.com";
        p.api_key = c.gemini_key;
    } else if (type == "grok") {
        p.kind = Kind::OpenAI;  // xAI is OpenAI-compatible
        p.name = "Grok";
        p.base_url = "https://api.x.ai";
        p.api_key = c.grok_key;
    } else if (type == "grok-sub") {
        p.kind = Kind::OpenAI;  // xAI API, but authorized by an OAuth subscription token
        p.name = "Grok (subscription)";
        p.base_url = "https://api.x.ai";
        // api_key is filled in at call time with a fresh OAuth access token.
    } else if (type == "custom") {
        p.kind = Kind::OpenAI;  // generic OpenAI-compatible endpoint
        p.name = "Custom";
        p.base_url = c.custom_base;
        p.api_key = c.custom_key;
    } else {
        p.kind = Kind::Ollama;
        p.name = "Ollama";
        p.base_url = c.host;
    }
    return p;
}

std::vector<std::pair<std::string, std::string>> configured_types(const Config& c) {
    std::vector<std::pair<std::string, std::string>> v;
    v.push_back({"ollama", "Ollama"});  // local server is always an option
    if (!c.openai_key.empty()) {
        v.push_back({"openai", "OpenAI"});
    }
    if (!c.anthropic_key.empty()) {
        v.push_back({"anthropic", "Anthropic"});
    }
    if (!c.gemini_key.empty()) {
        v.push_back({"gemini", "Gemini"});
    }
    if (!c.grok_key.empty()) {
        v.push_back({"grok", "Grok"});
    }
    if (grok_oauth::logged_in()) {
        v.push_back({"grok-sub", "Grok (subscription)"});
    }
    if (!c.custom_base.empty()) {
        v.push_back({"custom", "Custom"});
    }
    return v;
}

// --- shared helpers ---------------------------------------------------------

// Pull a human message out of an API "error" value (a string, or an object with
// a "message" field).
static std::string error_text(const nlohmann::json& e) {
    if (e.is_string()) {
        return e.get<std::string>();
    }
    if (e.is_object() && e.contains("message") && e["message"].is_string()) {
        return e["message"].get<std::string>();
    }
    return e.dump();
}

// Best-effort error message from a non-streaming (error) body.
static std::string extract_error(const std::string& raw) {
    try {
        auto j = nlohmann::json::parse(raw);
        if (j.contains("error")) {
            return error_text(j["error"]);
        }
        if (j.contains("message") && j["message"].is_string()) {
            return j["message"].get<std::string>();
        }
    } catch (const std::exception&) {
    }
    return "";
}

// Drives a Server-Sent-Events POST. OpenAI, Anthropic and Gemini all stream as
// `data: {json}` lines, so we share the line splitter and hand each decoded JSON
// object to `on_event`. on_event returns false (having set `error`) to abort.
static void run_sse(const std::string& base,
                    const httplib::Headers& headers,
                    const std::string& path,
                    const std::string& body,
                    std::string& error,
                    const std::function<bool(const nlohmann::json&)>& on_event,
                    ChatStats& stats,
                    const std::function<void(bool, std::string, ChatStats)>& on_done) {
    httplib::Client cli(base);
    cli.set_connection_timeout(15, 0);
    cli.set_read_timeout(300, 0);

    std::string buffer;  // partial line carry-over
    std::string raw;     // full body, kept for error extraction
    bool aborted = false;

    httplib::Request req;
    req.method = "POST";
    req.path = path;
    req.headers = headers;
    req.body = body;
    req.content_receiver = [&](const char* data, size_t len, uint64_t, uint64_t) -> bool {
        raw.append(data, len);
        buffer.append(data, len);
        size_t nl;
        while ((nl = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, nl);
            buffer.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.rfind("data:", 0) != 0) {
                continue;  // skip event:, comments, and blank separators
            }
            std::string payload = line.substr(5);
            const size_t s = payload.find_first_not_of(' ');
            payload = (s == std::string::npos) ? std::string() : payload.substr(s);
            if (payload.empty() || payload == "[DONE]") {
                continue;
            }
            try {
                auto obj = nlohmann::json::parse(payload);
                if (!on_event(obj)) {
                    aborted = true;
                    return false;  // on_event set `error`
                }
            } catch (const std::exception&) {
                // A partial or odd line: skip and wait for more.
            }
        }
        return true;
    };

    auto res = cli.send(req);

    if (aborted) {
        on_done(false, error, stats);
        return;
    }
    if (!res) {
        on_done(false, "connection failed: " + httplib::to_string(res.error()), stats);
        return;
    }
    if (res->status < 200 || res->status >= 300) {
        const std::string msg = extract_error(raw);
        on_done(false, msg.empty() ? ("HTTP " + std::to_string(res->status)) : msg, stats);
        return;
    }
    on_done(true, "", stats);
}

// --- chat -------------------------------------------------------------------

void chat_stream(const Provider& p,
                 const std::string& system,
                 const std::vector<ChatMessage>& messages,
                 const std::function<void(const std::string&)>& on_token,
                 const std::function<void(bool, std::string, ChatStats)>& on_done) {
    if (p.kind == Kind::Ollama) {
        std::vector<ChatMessage> full;
        if (!system.empty()) {
            full.push_back({"system", system});
        }
        for (const auto& m : messages) {
            full.push_back(m);
        }
        ollama::chat_stream(p.base_url, p.model, full, on_token, on_done);
        return;
    }

    ChatStats stats;
    std::string error;

    if (p.kind == Kind::OpenAI) {
        nlohmann::json body;
        body["model"] = p.model;
        body["stream"] = true;
        body["stream_options"] = {{"include_usage", true}};
        body["messages"] = nlohmann::json::array();
        if (!system.empty()) {
            body["messages"].push_back({{"role", "system"}, {"content", system}});
        }
        for (const auto& m : messages) {
            body["messages"].push_back({{"role", m.role}, {"content", m.content}});
        }
        httplib::Headers h{{"Content-Type", "application/json"}};
        if (!p.api_key.empty()) {  // a keyless local proxy (e.g. Hermes) needs none
            h.emplace("Authorization", "Bearer " + p.api_key);
        }
        run_sse(p.base_url, h, "/v1/chat/completions", body.dump(), error,
                [&](const nlohmann::json& o) -> bool {
                    if (o.contains("error")) {
                        error = error_text(o["error"]);
                        return false;
                    }
                    if (o.contains("choices") && !o["choices"].empty()) {
                        const auto& ch = o["choices"][0];
                        if (ch.contains("delta") && ch["delta"].contains("content") &&
                            ch["delta"]["content"].is_string()) {
                            const std::string tok = ch["delta"]["content"].get<std::string>();
                            if (!tok.empty()) {
                                on_token(tok);
                            }
                        }
                    }
                    if (o.contains("usage") && o["usage"].is_object()) {
                        stats.prompt_tokens = o["usage"].value("prompt_tokens", stats.prompt_tokens);
                        stats.eval_tokens = o["usage"].value("completion_tokens", stats.eval_tokens);
                    }
                    return true;
                },
                stats, on_done);
        return;
    }

    if (p.kind == Kind::Anthropic) {
        nlohmann::json body;
        body["model"] = p.model;
        body["max_tokens"] = 4096;
        body["stream"] = true;
        if (!system.empty()) {
            body["system"] = system;
        }
        body["messages"] = nlohmann::json::array();
        for (const auto& m : messages) {
            body["messages"].push_back({{"role", m.role}, {"content", m.content}});
        }
        httplib::Headers h{{"Content-Type", "application/json"},
                           {"x-api-key", p.api_key},
                           {"anthropic-version", "2023-06-01"}};
        run_sse(p.base_url, h, "/v1/messages", body.dump(), error,
                [&](const nlohmann::json& o) -> bool {
                    const std::string type = o.value("type", "");
                    if (type == "error") {
                        error = error_text(o.value("error", nlohmann::json::object()));
                        return false;
                    }
                    if (type == "message_start" && o.contains("message")) {
                        stats.prompt_tokens =
                            o["message"].value("usage", nlohmann::json::object())
                                .value("input_tokens", stats.prompt_tokens);
                    } else if (type == "content_block_delta" && o.contains("delta")) {
                        const auto& d = o["delta"];
                        if (d.value("type", "") == "text_delta") {
                            const std::string tok = d.value("text", "");
                            if (!tok.empty()) {
                                on_token(tok);
                            }
                        }
                    } else if (type == "message_delta" && o.contains("usage")) {
                        stats.eval_tokens = o["usage"].value("output_tokens", stats.eval_tokens);
                    }
                    return true;
                },
                stats, on_done);
        return;
    }

    // Gemini: roles are "user"/"model", the system prompt is a separate field,
    // and the API key rides in the query string.
    nlohmann::json body;
    body["contents"] = nlohmann::json::array();
    for (const auto& m : messages) {
        nlohmann::json part;
        part["text"] = m.content;
        nlohmann::json msg;
        msg["role"] = (m.role == "assistant") ? "model" : "user";
        msg["parts"] = nlohmann::json::array({part});
        body["contents"].push_back(msg);
    }
    if (!system.empty()) {
        nlohmann::json part;
        part["text"] = system;
        body["systemInstruction"]["parts"] = nlohmann::json::array({part});
    }
    httplib::Headers h{{"Content-Type", "application/json"}};
    const std::string path = "/v1beta/models/" + p.model +
                             ":streamGenerateContent?alt=sse&key=" + p.api_key;
    run_sse(p.base_url, h, path, body.dump(), error,
            [&](const nlohmann::json& o) -> bool {
                if (o.contains("error")) {
                    error = error_text(o["error"]);
                    return false;
                }
                if (o.contains("candidates") && !o["candidates"].empty()) {
                    const auto& cand = o["candidates"][0];
                    if (cand.contains("content") && cand["content"].contains("parts")) {
                        for (const auto& part : cand["content"]["parts"]) {
                            if (part.contains("text") && part["text"].is_string()) {
                                const std::string tok = part["text"].get<std::string>();
                                if (!tok.empty()) {
                                    on_token(tok);
                                }
                            }
                        }
                    }
                }
                if (o.contains("usageMetadata")) {
                    const auto& u = o["usageMetadata"];
                    stats.prompt_tokens = u.value("promptTokenCount", stats.prompt_tokens);
                    stats.eval_tokens = u.value("candidatesTokenCount", stats.eval_tokens);
                }
                return true;
            },
            stats, on_done);
}

// --- model listing ----------------------------------------------------------

std::vector<std::string> list_models(const Provider& p) {
    if (p.kind == Kind::Ollama) {
        return ollama::list_models(p.base_url);
    }

    std::vector<std::string> models;
    httplib::Client cli(p.base_url);
    cli.set_connection_timeout(10, 0);
    cli.set_read_timeout(15, 0);

    httplib::Headers h{{"Content-Type", "application/json"}};
    std::string path;
    if (p.kind == Kind::Anthropic) {
        h.emplace("x-api-key", p.api_key);
        h.emplace("anthropic-version", "2023-06-01");
        path = "/v1/models";
    } else if (p.kind == Kind::Gemini) {
        path = "/v1beta/models?key=" + p.api_key;
    } else {  // OpenAI / custom / grok
        if (!p.api_key.empty()) {
            h.emplace("Authorization", "Bearer " + p.api_key);
        }
        path = "/v1/models";
    }

    auto res = cli.Get(path, h);
    if (!res || res->status < 200 || res->status >= 300) {
        return models;
    }
    try {
        auto j = nlohmann::json::parse(res->body);
        if (p.kind == Kind::Gemini) {
            if (j.contains("models")) {
                for (const auto& m : j["models"]) {
                    std::string name = m.value("name", "");
                    const std::string pre = "models/";
                    if (name.rfind(pre, 0) == 0) {
                        name = name.substr(pre.size());
                    }
                    if (!name.empty()) {
                        models.push_back(name);
                    }
                }
            }
        } else if (j.contains("data")) {  // OpenAI + Anthropic both use { data: [{id}] }
            for (const auto& m : j["data"]) {
                if (m.contains("id")) {
                    models.push_back(m["id"].get<std::string>());
                }
            }
        }
    } catch (const std::exception&) {
    }
    return models;
}

} // namespace provider
