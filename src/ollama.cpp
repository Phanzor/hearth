#include "ollama.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace ollama {

void chat_stream(const std::string& host,
                 const std::string& model,
                 const std::vector<ChatMessage>& messages,
                 const std::function<void(const std::string&)>& on_token,
                 const std::function<void(bool, std::string, ChatStats)>& on_done) {
    nlohmann::json body;
    body["model"] = model;
    body["stream"] = true;
    body["messages"] = nlohmann::json::array();
    for (const auto& m : messages) {
        body["messages"].push_back({{"role", m.role}, {"content", m.content}});
    }

    httplib::Client cli(host);
    cli.set_connection_timeout(10, 0);
    cli.set_read_timeout(300, 0);

    // Ollama streams newline-delimited JSON: one object per line. We buffer
    // partial reads and parse each complete line as it arrives. httplib has no
    // response-streaming Post overload, so we drive it through a Request whose
    // content_receiver is fed each chunk as the body downloads.
    std::string buffer;
    std::string error;
    bool failed = false;
    ChatStats stats;

    httplib::Request req;
    req.method = "POST";
    req.path = "/api/chat";
    req.headers = {{"Content-Type", "application/json"}};
    req.body = body.dump();
    req.content_receiver = [&](const char* data, size_t len, uint64_t, uint64_t) -> bool {
        buffer.append(data, len);
        size_t nl;
        while ((nl = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, nl);
            buffer.erase(0, nl + 1);
            if (line.empty()) {
                continue;
            }
            try {
                auto obj = nlohmann::json::parse(line);
                if (obj.contains("error")) {
                    error = obj["error"].get<std::string>();
                    failed = true;
                    return false;
                }
                if (obj.contains("message") && obj["message"].contains("content")) {
                    const std::string tok = obj["message"]["content"].get<std::string>();
                    if (!tok.empty()) {
                        on_token(tok);
                    }
                }
                if (obj.value("done", false)) {
                    stats.prompt_tokens = obj.value("prompt_eval_count", 0);
                    stats.eval_tokens = obj.value("eval_count", 0);
                }
            } catch (const std::exception&) {
                // A complete line should always parse; skip anything odd.
            }
        }
        return true;
    };

    auto res = cli.send(req);

    if (failed) {
        on_done(false, error, stats);
        return;
    }
    if (!res) {
        on_done(false, "connection failed: " + httplib::to_string(res.error()), stats);
        return;
    }
    if (res->status < 200 || res->status >= 300) {
        on_done(false, "HTTP " + std::to_string(res->status), stats);
        return;
    }
    on_done(true, "", stats);
}

std::vector<std::string> list_models(const std::string& host) {
    std::vector<std::string> models;
    httplib::Client cli(host);
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(10, 0);

    auto res = cli.Get("/api/tags");
    if (!res || res->status < 200 || res->status >= 300) {
        return models;
    }
    try {
        auto j = nlohmann::json::parse(res->body);
        if (j.contains("models")) {
            for (const auto& m : j["models"]) {
                if (m.contains("name")) {
                    models.push_back(m["name"].get<std::string>());
                }
            }
        }
    } catch (const std::exception&) {
        // Leave models empty on a parse error.
    }
    return models;
}

} // namespace ollama
