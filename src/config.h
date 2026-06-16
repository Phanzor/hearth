#pragma once

#include <string>

// Persisted user settings. Defaults target a stock local Ollama install.
struct Config {
    std::string host = "http://localhost:11434";
    std::string model = "qwen2.5:7b";
    std::string theme = "midnight";
    std::string system_prompt;            // prepended to every chat when enabled
    bool system_prompt_enabled = true;    // off = rely on the model's built-in prompt
};

std::string config_path();
Config load_config();
bool save_config(const Config& c);
