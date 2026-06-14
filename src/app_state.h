#pragma once

#include <string>
#include <vector>

#include "config.h"
#include "theme.h"

struct Message {
    std::string role;     // "user" | "assistant" | "system"
    std::string content;
};

// Single source of truth for the running app. Lives for the whole program in
// main(); the UI reads it and the chat worker mutates it (only ever from the
// UI thread, via ScreenInteractive::Post).
struct AppState {
    Config config;
    Theme theme = theme_by_name("dark");
    std::vector<Message> messages;
    std::vector<std::string> models;  // installed models, fetched from the server
    std::string input;
    int tab_index = 0;        // 0 = Chat, 1 = Settings
    bool streaming = false;
    std::string status;
    float scroll = 1.0f;      // history scroll: 0 = top, 1 = bottom (follow)
    int palette_sel = 0;      // highlighted item in the slash palette
    int input_cursor = 0;     // chat input cursor position (for autofill)

    // Token accounting, reported by Ollama on each completed response.
    int last_in = 0;          // prompt tokens of the last exchange
    int last_out = 0;         // generated tokens of the last exchange
    long total_in = 0;        // session totals
    long total_out = 0;
};
