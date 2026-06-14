#pragma once

#include <string>

// Persisted user settings. Defaults target a stock local Ollama install.
struct Config {
    std::string host = "http://localhost:11434";
    std::string model = "qwen2.5:7b";
    std::string theme = "dark";
};

std::string config_path();
Config load_config();
bool save_config(const Config& c);
