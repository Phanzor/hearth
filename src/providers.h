#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "config.h"
#include "ollama.h"

// A thin abstraction over the chat backends Hearth can talk to. Ollama keeps its
// native /api/chat transport (see ollama.{h,cpp}); the cloud providers share an
// SSE transport here. Each one is resolved from the persisted Config at send
// time, so switching providers is just a config change.
namespace provider {

enum class Kind { Ollama, OpenAI, Anthropic, Gemini };

// A fully resolved target: where to send, what to send as, and the model/key.
struct Provider {
    Kind kind = Kind::Ollama;
    std::string name;       // human label, e.g. "OpenAI"
    std::string base_url;   // scheme + host, e.g. https://api.openai.com
    std::string api_key;    // empty for Ollama
    std::string model;
};

using ollama::ChatMessage;
using ollama::ChatStats;

// Build a fully-resolved target for `type` (ollama|openai|anthropic|gemini|
// custom) and `model`, pulling keys/base URLs from `c`.
Provider make(const Config& c, const std::string& type, const std::string& model);

// The providers with a usable connection, as (id, label) - Ollama is always
// listed; a cloud provider appears once its key (or, for custom, base URL) is set.
std::vector<std::pair<std::string, std::string>> configured_types(const Config& c);

// Streaming chat against `p`. `system` is the resolved system prompt (may be
// empty); `messages` holds only the user/assistant turns - each transport places
// the system prompt where its API expects it. Blocking; run on a worker thread.
void chat_stream(const Provider& p,
                 const std::string& system,
                 const std::vector<ChatMessage>& messages,
                 const std::function<void(const std::string&)>& on_token,
                 const std::function<void(bool ok, std::string error, ChatStats stats)>& on_done);

// Best-effort model listing for `p` (empty if it can't be reached or has no
// listing endpoint). Blocking; run on a worker thread.
std::vector<std::string> list_models(const Provider& p);

} // namespace provider
