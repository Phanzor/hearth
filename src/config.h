#pragma once

#include <string>

// Persisted user settings. Defaults target a stock local Ollama install.
struct Config {
    std::string host = "http://localhost:11434";   // Ollama base URL (local)
    std::string theme = "midnight";
    std::string system_prompt;            // prepended to every chat when enabled
    bool system_prompt_enabled = true;    // off = rely on the model's built-in prompt

    // Default backend + model for a brand-new chat; each chat then remembers its
    // own choice. provider is one of "ollama" | "openai" | "anthropic" |
    // "gemini" | "grok" | "grok-sub" | "custom".
    std::string provider = "ollama";
    std::string model = "qwen2.5:7b";

    // Cloud connections (keys stored here in plaintext). Fill in whichever you
    // use; per-chat models are chosen with /model <type> <model>.
    std::string openai_key;
    std::string anthropic_key;
    std::string gemini_key;
    std::string grok_key;  // Grok via the xAI API (api.x.ai), OpenAI-compatible
    // Grok via a SuperGrok / X Premium+ subscription uses native OAuth instead of
    // a key; tokens live outside config (see grok_oauth).
    // Generic OpenAI-compatible endpoint (OpenRouter, Groq, Mistral, a local
    // /v1 server, ...): its own base URL plus key.
    std::string custom_base;
    std::string custom_key;
};

std::string config_path();
Config load_config();
bool save_config(const Config& c);
