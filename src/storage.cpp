#include "storage.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace storage {

namespace {

fs::path data_dir() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && *xdg) {
        return fs::path(xdg) / "hearth";
    }
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : ".") / ".local" / "share" / "hearth";
}

fs::path conv_dir() { return data_dir() / "conversations"; }
fs::path arch_dir() { return data_dir() / "archived"; }

nlohmann::json to_json(const Conversation& c) {
    nlohmann::json j;
    j["id"] = c.id;
    j["title"] = c.title;
    j["provider"] = c.provider;
    j["model"] = c.model;
    j["last_in"] = c.last_in;
    j["last_out"] = c.last_out;
    j["total_in"] = c.total_in;
    j["total_out"] = c.total_out;
    j["messages"] = nlohmann::json::array();
    for (const auto& m : c.messages) {
        j["messages"].push_back({{"role", m.role}, {"content", m.content}});
    }
    return j;
}

Conversation from_json(const nlohmann::json& j) {
    Conversation c;
    c.id = j.value("id", "");
    c.title = j.value("title", "");
    c.provider = j.value("provider", "");
    c.model = j.value("model", "");
    c.last_in = j.value("last_in", 0);
    c.last_out = j.value("last_out", 0);
    c.total_in = j.value("total_in", 0L);
    c.total_out = j.value("total_out", 0L);
    if (j.contains("messages")) {
        for (const auto& m : j["messages"]) {
            c.messages.push_back({m.value("role", ""), m.value("content", "")});
        }
    }
    return c;
}

void load_dir(const fs::path& dir, std::vector<Conversation>& out) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        return;
    }
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        try {
            std::ifstream in(entry.path());
            nlohmann::json j;
            in >> j;
            out.push_back(from_json(j));
        } catch (const std::exception&) {
            // Skip an unreadable file rather than failing the whole load.
        }
    }
    std::sort(out.begin(), out.end(),
              [](const Conversation& a, const Conversation& b) { return a.id < b.id; });
}

std::string sanitize(const std::string& s) {
    std::string out;
    for (char ch : s) {
        out += (std::isalnum((unsigned char)ch) || ch == '-' || ch == '_') ? ch : '_';
    }
    if (out.empty()) {
        out = "chat";
    }
    return out.substr(0, 60);
}

} // namespace

std::string new_id() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    return std::to_string(us);
}

void load(std::vector<Conversation>& active, std::vector<Conversation>& archived) {
    load_dir(conv_dir(), active);
    load_dir(arch_dir(), archived);
}

void save(const Conversation& c, bool archived) {
    if (c.id.empty()) {
        return;
    }
    try {
        const fs::path dir = archived ? arch_dir() : conv_dir();
        fs::create_directories(dir);
        std::ofstream out(dir / (c.id + ".json"));
        out << to_json(c).dump(2);
    } catch (const std::exception&) {
        // Best-effort; ignore write failures.
    }
}

void erase(const std::string& id) {
    if (id.empty()) {
        return;
    }
    std::error_code ec;
    fs::remove(conv_dir() / (id + ".json"), ec);
    fs::remove(arch_dir() / (id + ".json"), ec);
}

std::string export_dir() {
    const char* home = std::getenv("HOME");
    return (fs::path(home ? home : ".") / "hearth-exports").string();
}

std::string export_markdown(const Conversation& c) {
    try {
        const fs::path dir = export_dir();
        fs::create_directories(dir);
        const std::string name =
            sanitize(c.title.empty() ? c.id : c.title) + ".md";
        const fs::path path = dir / name;
        std::ofstream out(path);
        out << "# " << (c.title.empty() ? "Untitled chat" : c.title) << "\n\n";
        for (const auto& m : c.messages) {
            if (m.role == "user") {
                out << "**You:** " << m.content << "\n\n";
            } else if (m.role == "assistant") {
                out << m.content << "\n\n";
            }
        }
        return path.string();
    } catch (const std::exception&) {
        return "";
    }
}

} // namespace storage
