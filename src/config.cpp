#include "config.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static fs::path config_dir() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        return fs::path(xdg) / "hearth";
    }
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : ".") / ".config" / "hearth";
}

std::string config_path() {
    return (config_dir() / "config.json").string();
}

Config load_config() {
    Config c;
    std::ifstream in(config_path());
    if (!in) {
        return c;
    }
    try {
        nlohmann::json j;
        in >> j;
        if (j.contains("host")) {
            c.host = j["host"].get<std::string>();
        }
        if (j.contains("model")) {
            c.model = j["model"].get<std::string>();
        }
        if (j.contains("theme")) {
            c.theme = j["theme"].get<std::string>();
        }
        if (j.contains("system_prompt")) {
            c.system_prompt = j["system_prompt"].get<std::string>();
        }
        if (j.contains("system_prompt_enabled")) {
            c.system_prompt_enabled = j["system_prompt_enabled"].get<bool>();
        }
    } catch (const std::exception&) {
        // Corrupt config: fall back to defaults rather than crashing.
    }
    return c;
}

bool save_config(const Config& c) {
    try {
        fs::create_directories(config_dir());
        nlohmann::json j{{"host", c.host},
                         {"model", c.model},
                         {"theme", c.theme},
                         {"system_prompt", c.system_prompt},
                         {"system_prompt_enabled", c.system_prompt_enabled}};
        std::ofstream out(config_path());
        if (!out) {
            return false;
        }
        out << j.dump(2);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
