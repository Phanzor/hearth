#pragma once

#include <string>
#include <vector>

#include "config.h"
#include "theme.h"

struct Message {
    std::string role;     // "user" | "assistant" | "system"
    std::string content;
};

// One chat thread: its messages, scroll position, and token tallies. The title
// is empty until the first user message, then derived from it. `id` is a stable
// key (used as the on-disk filename) assigned when the chat is first saved.
struct Conversation {
    std::string id;
    std::string title;
    std::vector<Message> messages;
    int last_in = 0;
    int last_out = 0;
    long total_in = 0;
    long total_out = 0;
    float scroll = 1.0f;  // 0 = top, 1 = bottom (follow); not persisted
};

// Single source of truth for the running app. Lives for the whole program in
// main(); the UI reads it and the chat worker mutates it (only ever from the
// UI thread, via ScreenInteractive::Post).
struct AppState {
    Config config;
    Theme theme = theme_by_name("dark");
    std::vector<std::string> models;  // installed models, fetched from the server

    // Active chats shown in the tree. active_conv == -1 means the "New Chat"
    // draft. Streaming workers target a chat by id, so list mutations are safe.
    std::vector<Conversation> conversations;
    std::vector<Conversation> archived;  // hidden from the tree; managed in Settings
    Conversation draft;       // the empty chat shown behind "New Chat"
    int active_conv = -1;     // which conversation the chat view shows (-1 = draft)

    int view = 0;             // content shown: 0 = chat, 1 = settings
    int sidebar_sel = 0;      // highlighted sidebar row (0 = New Chat)
    int settings_sel = 0;     // highlighted settings row (3+ = archived chats)

    // Delete/archive confirmation popup.
    int popup = 0;            // 0 = none, 1 = confirm delete/archive
    int popup_sel = 0;        // selected popup button
    int popup_conv = -1;      // conversation index the popup targets

    std::string input;
    int input_cursor = 0;     // chat input cursor position (for autofill)
    int palette_sel = 0;      // highlighted item in the slash palette
    bool streaming = false;
    bool extended_keys = false;        // extended keyboard protocol active (detected/forced on)
    bool extended_keys_probed = false; // startup capability probe already done
    std::string status;

    Conversation& active() { return active_conv < 0 ? draft : conversations[active_conv]; }
};
