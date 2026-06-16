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
    Theme theme = theme_by_name("midnight");
    std::vector<std::string> models;  // installed models, fetched from the server

    // Active chats shown in the tree. active_conv == -1 means the "New Chat"
    // draft. Streaming workers target a chat by id, so list mutations are safe.
    std::vector<Conversation> conversations;
    std::vector<Conversation> archived;  // hidden from the tree; managed in Settings
    Conversation draft;       // the empty chat shown behind "New Chat"
    int active_conv = -1;     // which conversation the chat view shows (-1 = draft)

    int view = 0;             // content shown: 0 = chat, 1 = settings
    int sidebar_sel = 0;      // highlighted sidebar row (0 = New Chat)
    int settings_view = 0;    // settings subview: 0 = General, 1 = Connections, 2 = Themes, 3 = Archive
    int settings_sel = 0;     // highlighted row within the current settings subview

    // Custom theme editor, shown inside the Themes subview. While editing, the
    // draft is applied live (state.theme) so the whole UI previews it.
    bool theme_editing = false;
    Theme theme_draft;            // the theme being created or edited
    std::string theme_edit_orig;  // name of the theme being edited ("" = brand new)
    std::string theme_prev;       // theme to restore (by name) if the edit is cancelled
    int theme_edit_sel = 0;       // focused editor row: 0 name, 1..N palette slots, then Save, Cancel
    int theme_name_cursor = 0;    // cursor within theme_draft.name
    float edit_h = 0.0f;          // hue (0-360) of the focused color slot
    float edit_s = 0.0f;          // saturation (0-1)
    float edit_v = 0.0f;          // value/brightness (0-1)
    bool theme_hex = false;       // typing a hex code for the focused slot
    std::string hex_buf;          // the hex being typed

    // Action popup. 0 = none, 1 = active-chat menu, 2 = archived-chat menu,
    // 3 = rename active chat, 4 = rename archived chat.
    int popup = 0;
    int popup_sel = 0;        // selected popup button
    int popup_conv = -1;      // conversation index the popup targets
    std::string rename_buf;   // editable title while a rename popup is open
    int rename_cursor = 0;    // cursor (byte index) within rename_buf

    std::string input;
    int input_cursor = 0;     // chat input cursor position (for autofill)
    int palette_sel = 0;      // highlighted item in the slash palette
    bool streaming = false;
    bool extended_keys = false;        // extended keyboard protocol active (detected/forced on)
    bool extended_keys_probed = false; // startup capability probe already done
    bool pasting = false;              // currently inside a bracketed-paste sequence
    std::string paste_buf;             // accumulates pasted text until the end marker
    std::string status;

    Conversation& active() { return active_conv < 0 ? draft : conversations[active_conv]; }
};
