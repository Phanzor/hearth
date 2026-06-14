#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ollama {

struct ChatMessage {
    std::string role;     // "user" | "assistant" | "system"
    std::string content;
};

// Token counts Ollama reports in its final (done) message.
struct ChatStats {
    int prompt_tokens = 0;
    int eval_tokens = 0;
};

// Blocking, streaming call to Ollama's /api/chat. Runs the request on the
// calling thread and invokes on_token for each generated chunk, then on_done
// exactly once (with token stats). Intended to be launched on a worker thread;
// the callbacks must marshal back to the UI thread themselves (see ui.cpp).
void chat_stream(const std::string& host,
                 const std::string& model,
                 const std::vector<ChatMessage>& messages,
                 const std::function<void(const std::string&)>& on_token,
                 const std::function<void(bool ok, std::string error, ChatStats stats)>& on_done);

// Blocking GET /api/tags. Returns the names of the models installed on the
// server, or an empty list if it can't be reached. Run on a worker thread.
std::vector<std::string> list_models(const std::string& host);

} // namespace ollama
