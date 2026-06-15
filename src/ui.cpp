#include "ui.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/flexbox_config.hpp>
#include <ftxui/screen/box.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "config.h"
#include "markdown.h"
#include "ollama.h"
#include "storage.h"
#include "theme.h"

using namespace ftxui;

// --- Extended keyboard protocol ---------------------------------------------
// The keyboard protocol introduced by kitty and now implemented by many other
// terminals (foot, Ghostty, WezTerm, recent iTerm2, ...). FTXUI can't parse it,
// but it passes the unrecognized CSI-u key reports through as Special events, so
// we push the "disambiguate escape codes" flag to get modified keys (notably
// Shift+Enter) reported distinctly, then translate every report back into the
// legacy event the rest of the app already understands. The flag stack is
// per-screen, so this only affects our alternate screen, never the shell; we pop
// it on exit. We enable it only after the terminal confirms support (see
// probe_extended_keys), so terminals that lack it stay in plain legacy mode.

// The terminal's reply to our capability query: "ESC [ ? <flags> u". Only
// terminals that speak the protocol send this, so it is our signal that
// enabling it is safe. Legacy terminals never answer, and so never get touched.
static bool is_extended_keys_reply(const std::string& s) {
    return s.size() >= 4 && s[0] == '\x1b' && s[1] == '[' && s[2] == '?' && s.back() == 'u';
}

// Runs once, on the first event (which lands after FTXUI is on the alternate
// screen). HEARTH_EXTENDED_KEYS forces the decision (1 = on, 0 = off); when unset
// we query the terminal and only enable on its reply, so an unsupported terminal
// is left entirely in legacy mode.
static void probe_extended_keys(AppState& state) {
    if (state.extended_keys_probed) {
        return;
    }
    state.extended_keys_probed = true;
    // Bracketed paste is universally safe to ask for: terminals that don't speak
    // it ignore the request, and it lets us take a multi-line paste verbatim.
    std::cout << "\x1b[?2004h" << std::flush;
    const char* env = std::getenv("HEARTH_EXTENDED_KEYS");
    if (env && env[0] == '0') {
        return;  // forced off
    }
    if (env && env[0]) {
        std::cout << "\x1b[>1u" << std::flush;  // forced on (skip detection)
        state.extended_keys = true;
        return;
    }
    std::cout << "\x1b[?u" << std::flush;  // auto: ask, then decide on the reply
}

static void disable_extended_keys(AppState& state) {
    if (!state.extended_keys) {
        return;
    }
    std::cout << "\x1b[<u" << std::flush;  // pop our pushed flag
    state.extended_keys = false;
}

// Undo the terminal modes we turned on, on the way out.
static void restore_terminal(AppState& state) {
    std::cout << "\x1b[?2004l" << std::flush;  // disable bracketed paste
    disable_extended_keys(state);
}

static std::string base64_encode(const std::string& in) {
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    int val = 0, bits = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        bits += 8;
        while (bits >= 0) {
            out.push_back(tbl[(val >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6) {
        out.push_back(tbl[((val << 8) >> (bits + 8)) & 0x3F]);
    }
    while (out.size() % 4) {
        out.push_back('=');
    }
    return out;
}

// Copy text to the system clipboard via OSC 52. The app grabs the mouse (for
// wheel scrolling), so a drag never reaches the terminal's own selection; we
// take FTXUI's selection and put it on the clipboard ourselves instead. Works
// on kitty, foot, Ghostty, WezTerm, recent iTerm2/xterm.
static void copy_to_clipboard(const std::string& text) {
    std::cout << "\x1b]52;c;" << base64_encode(text) << "\x07" << std::flush;
}

static std::string utf8_encode(int cp) {
    std::string s;
    if (cp < 0x80) {
        s += static_cast<char>(cp);
    } else if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return s;
}

// Parse exactly one "ESC [ n (;n)* final" sequence. nums gets the numeric
// params (missing -> 0); returns false for anything else (mouse, private, etc.).
static bool parse_csi(const std::string& s, std::vector<int>& nums, char& final) {
    nums.clear();
    if (s.size() < 3 || s[0] != '\x1b' || s[1] != '[') {
        return false;
    }
    int cur = 0;
    bool has = false;
    for (size_t i = 2; i < s.size(); ++i) {
        const char c = s[i];
        if (c >= '0' && c <= '9') {
            cur = cur * 10 + (c - '0');
            has = true;
        } else if (c == ';') {
            nums.push_back(has ? cur : 0);
            cur = 0;
            has = false;
        } else if (c >= '@' && c <= '~') {
            nums.push_back(has ? cur : 0);
            final = c;
            return i + 1 == s.size();  // the final byte must end the string
        } else {
            return false;  // '<' (mouse), '?' (private), etc. - not ours
        }
    }
    return false;
}

// Translate a CSI-u (or modified-legacy) key report into the equivalent legacy
// event/action, reusing the app's existing handlers. Returns true if it consumed
// the event. Unmodified legacy sequences (which FTXUI already maps) carry no
// modifier param and are deliberately left alone, which also keeps the posted
// events from looping back through here.
static bool translate_key(const std::string& in, AppState& state, ScreenInteractive& screen) {
    std::vector<int> nums;
    char fin = 0;
    if (!parse_csi(in, nums, fin)) {
        return false;
    }
    const int code = nums.empty() ? 0 : nums[0];
    const int mods = nums.size() >= 2 ? nums[1] : 1;
    const int m = mods - 1;
    const bool shift = m & 1, alt = m & 2, ctrl = m & 4;

    if (fin == 'u') {  // a CSI-u key report; always protocol-specific
        if (ctrl && (code == 'c' || code == 'C')) {  // Ctrl+C quits
            restore_terminal(state);
            screen.ExitLoopClosure()();
            return true;
        }
        switch (code) {
            case 27: screen.PostEvent(Event::Escape); return true;
            case 9:  screen.PostEvent(shift ? Event::TabReverse : Event::Tab); return true;
            case 8:
            case 127: screen.PostEvent(Event::Backspace); return true;
            case 13:  // modified Enter -> newline (chat input matches "\x1b\r"); plain -> submit
                screen.PostEvent((shift || alt || ctrl) ? Event::Special("\x1b\r") : Event::Return);
                return true;
            default: break;
        }
        if (!ctrl && !alt && code >= 32) {  // a printable key -> character
            screen.PostEvent(Event::Character(utf8_encode(code)));
            return true;
        }
        return true;  // swallow modified combos we don't otherwise use
    }

    // Modified legacy functional keys ("ESC [ 1 ; mods letter" / "ESC [ n ; mods ~").
    // Unmodified ones have no modifier param (nums.size() < 2) and fall through to
    // FTXUI untouched; we map the modified forms to their plain events.
    if (nums.size() < 2) {
        return false;
    }
    switch (fin) {
        case 'A': screen.PostEvent(Event::ArrowUp);    return true;
        case 'B': screen.PostEvent(Event::ArrowDown);  return true;
        case 'C': screen.PostEvent(Event::ArrowRight); return true;
        case 'D': screen.PostEvent(Event::ArrowLeft);  return true;
        case 'H': screen.PostEvent(Event::Home);       return true;
        case 'F': screen.PostEvent(Event::End);        return true;
        case '~':
            switch (code) {
                case 3: screen.PostEvent(Event::Delete);   return true;
                case 5: screen.PostEvent(Event::PageUp);   return true;
                case 6: screen.PostEvent(Event::PageDown); return true;
                default: return true;
            }
        default: return false;
    }
}

// Find an active conversation by its stable id (streaming workers target by id
// so the list can be mutated while a reply is in flight).
static Conversation* find_conv(AppState& state, const std::string& id) {
    for (auto& c : state.conversations) {
        if (c.id == id) {
            return &c;
        }
    }
    return nullptr;
}

// A text-field transform without the default focused "inverted" background.
static std::function<Element(InputState)> plain_input(Color fg) {
    return [fg](InputState s) {
        s.element |= color(fg);
        if (s.is_placeholder) {
            s.element |= dim;
        }
        return s.element;
    };
}

// A wrapping line like paragraph(), but each space stays attached to its word
// instead of being a flexbox gap, so a mouse selection copies the spaces too
// (FTXUI's selection can't see layout gaps, only characters in text elements).
static Element selectable_paragraph(const std::string& line) {
    Elements words;
    size_t start = 0;
    while (start < line.size()) {
        const size_t sp = line.find(' ', start);
        const size_t len = (sp == std::string::npos) ? std::string::npos : sp - start + 1;
        words.push_back(text(line.substr(start, len)));
        if (sp == std::string::npos) {
            break;
        }
        start = sp + 1;
    }
    if (words.empty()) {
        words.push_back(text(""));
    }
    return flexbox(std::move(words));
}

// Claude-Code style: the user's turn is a highlighted block; everything else is
// the model and renders as Markdown with no "You/Assistant" labels. System
// notices (slash-command output) render dimmed.
static Element render_message(const Theme& t, const Message& m) {
    if (m.role == "user") {
        // Render each line as its own wrapping paragraph so explicit newlines
        // (Alt+Enter / Shift+Enter) are preserved instead of collapsing.
        Elements lines;
        std::stringstream ss(m.content);
        std::string ln;
        while (std::getline(ss, ln)) {
            lines.push_back(selectable_paragraph(ln) | color(t.user_fg) | bold);
        }
        if (lines.empty()) {
            lines.push_back(text(" "));
        }
        return hbox({
            text(" ❯ ") | color(t.accent) | bold,
            vbox(std::move(lines)) | flex,
        }) | bgcolor(t.user_bg);
    }
    if (m.role == "system") {
        Elements lines;
        std::stringstream ss(m.content);
        std::string ln;
        while (std::getline(ss, ln)) {
            lines.push_back(text(ln) | color(t.text_dim));
        }
        return hbox({text("   "), vbox(std::move(lines))});
    }
    return hbox({
        text("   "),
        render_markdown(t, m.content) | flex,
    });
}

static bool blank(const std::string& s) {
    return s.find_first_not_of(" \t\r\n") == std::string::npos;
}

// Kicks off a streaming request on a worker thread. All state mutation happens
// back on the UI thread via screen.Post, so no locking is needed.
static void send_message(AppState& state, ScreenInteractive& screen) {
    if (state.streaming || blank(state.input)) {
        return;
    }

    // Sending from the blank "New Chat" promotes the draft into a saved chat.
    if (state.active_conv < 0) {
        Conversation nc;
        nc.id = storage::new_id();
        state.conversations.push_back(std::move(nc));
        state.active_conv = static_cast<int>(state.conversations.size()) - 1;
        state.sidebar_sel = 1 + state.active_conv;
        state.draft = Conversation{};
    }

    Conversation& c = state.active();
    if (c.title.empty()) {
        c.title = state.input.substr(0, 40);  // name the chat after its first line
    }
    c.messages.push_back({"user", state.input});
    c.messages.push_back({"assistant", ""}); // placeholder we stream into
    state.input.clear();
    state.input_cursor = 0;
    state.streaming = true;
    c.scroll = 1.0f;      // jump to the bottom to follow the reply
    state.status.clear(); // the streaming indicator conveys progress
    storage::save(c, false);  // persist the user turn before the reply arrives

    std::vector<ollama::ChatMessage> request;
    for (size_t i = 0; i + 1 < c.messages.size(); ++i) {
        request.push_back({c.messages[i].role, c.messages[i].content});
    }
    const std::string host = state.config.host;
    const std::string model = state.config.model;
    const std::string cid = c.id;  // target this chat by id, even if the user switches

    std::thread([&state, &screen, request, host, model, cid] {
        ollama::chat_stream(
            host, model, request,
            [&state, &screen, cid](const std::string& tok) {
                screen.Post([&state, cid, tok] {
                    if (Conversation* conv = find_conv(state, cid)) {
                        if (!conv->messages.empty()) {
                            conv->messages.back().content += tok;
                        }
                    }
                });
            },
            [&state, &screen, cid](bool ok, std::string err, ollama::ChatStats stats) {
                screen.Post([&state, cid, ok, err, stats] {
                    state.streaming = false;
                    state.status = ok ? "" : ("error: " + err);
                    Conversation* conv = find_conv(state, cid);
                    if (!conv) {
                        return;  // chat was deleted/archived mid-reply
                    }
                    if (ok) {
                        conv->last_in = stats.prompt_tokens;
                        conv->last_out = stats.eval_tokens;
                        conv->total_in += stats.prompt_tokens;
                        conv->total_out += stats.eval_tokens;
                    } else if (!conv->messages.empty() && conv->messages.back().content.empty()) {
                        conv->messages.back().content = "(no response)";
                    }
                    storage::save(*conv, false);  // persist the completed exchange
                });
                screen.PostEvent(Event::Custom);
            });
    }).detach();
}

// Fetches the installed model list in the background and stores it in state.
static void refresh_models(AppState& state, ScreenInteractive& screen) {
    const std::string host = state.config.host;
    std::thread([&state, &screen, host] {
        auto models = ollama::list_models(host);
        screen.Post([&state, models] { state.models = models; });
        screen.PostEvent(Event::Custom);
    }).detach();
}

struct SlashCmd {
    const char* name;
    const char* desc;
};

static const std::vector<SlashCmd> kCommands = {
    {"help", "show available commands"},
    {"delete", "delete the current chat"},
    {"archive", "archive the current chat"},
    {"model", "pick a model (lists installed)"},
    {"quit", "exit Hearth"},
};

// Defined further down (after the sidebar); used by the slash handler.
static void delete_conv(AppState& state, int idx);
static void archive_conv(AppState& state, int idx);

// Case-insensitive subsequence match, so "/he" and "/hlp" both match "help".
static bool fuzzy_match(const std::string& q, const std::string& name) {
    size_t qi = 0;
    for (char c : name) {
        if (qi < q.size() &&
            std::tolower((unsigned char)c) == std::tolower((unsigned char)q[qi])) {
            qi++;
        }
    }
    return qi == q.size();
}

// Resolve a typed model query to a full installed model name (exact match, else
// the first fuzzy match, else the literal text as a fallback).
static std::string resolve_model(const AppState& state, const std::string& q) {
    for (const auto& m : state.models) {
        if (m == q) {
            return m;
        }
    }
    for (const auto& m : state.models) {
        if (fuzzy_match(q, m)) {
            return m;
        }
    }
    return q;
}

// Handles a line beginning with '/'. Unknown commands report an error.
static void run_slash(AppState& state, ScreenInteractive& screen, const std::string& line) {
    const size_t sp = line.find(' ');
    const std::string cmd = line.substr(1, sp == std::string::npos ? std::string::npos : sp - 1);
    std::string args;
    if (sp != std::string::npos) {
        args = line.substr(sp + 1);
        const size_t a = args.find_first_not_of(" \t");
        const size_t b = args.find_last_not_of(" \t");
        args = (a == std::string::npos) ? "" : args.substr(a, b - a + 1);
    }

    Conversation& conv = state.active();
    conv.scroll = 1.0f;
    if (cmd == "delete") {
        if (state.active_conv >= 0) {
            delete_conv(state, state.active_conv);  // returns to a blank draft
            state.status = "chat deleted";
        } else {
            state.status = "no active chat to delete";
        }
    } else if (cmd == "archive") {
        if (state.active_conv >= 0) {
            archive_conv(state, state.active_conv);  // moves it to archived, blank draft
            state.status = "chat archived";
        } else {
            state.status = "no active chat to archive";
        }
    } else if (cmd == "help") {
        std::string txt = "Commands:";
        for (const auto& c : kCommands) {
            txt += "\n  /" + std::string(c.name) + "   " + c.desc;
        }
        conv.messages.push_back({"system", txt});
    } else if (cmd == "model") {
        if (args.empty()) {
            if (state.models.empty()) {
                state.status = "no models found - check the host in Settings";
            } else {
                std::string txt = "Installed models:";
                for (const auto& m : state.models) {
                    txt += "\n  " + m + (m == state.config.model ? "  (current)" : "");
                }
                conv.messages.push_back({"system", txt});
            }
        } else {
            const std::string chosen = resolve_model(state, args);
            state.config.model = chosen;
            save_config(state.config);  // remember the choice across restarts
            state.status = "model set to " + chosen;
        }
    } else if (cmd == "quit" || cmd == "exit") {
        restore_terminal(state);
        screen.ExitLoopClosure()();
    } else {
        state.status = "error: unknown command /" + cmd;
    }
}

// One row of the slash palette. `fill` is what replaces the input when chosen;
// `run` means Enter should also execute it (a complete choice, e.g. a model).
struct PaletteItem {
    std::string label;
    std::string desc;
    std::string fill;
    bool run;
    bool is_model;
};

// The palette shown for the current input: matching commands, or - once the
// command is "/model" - the installed models filtered by what's typed.
static std::vector<PaletteItem> compute_palette(const AppState& state) {
    std::vector<PaletteItem> out;
    const std::string& in = state.input;
    if (in.empty() || in.front() != '/') {
        return out;
    }
    const std::string rest = in.substr(1);
    const size_t sp = rest.find(' ');
    const std::string cmd = (sp == std::string::npos) ? rest : rest.substr(0, sp);
    const std::string args = (sp == std::string::npos) ? "" : rest.substr(sp + 1);

    if (cmd == "model") {
        for (const auto& m : state.models) {
            if (fuzzy_match(args, m)) {
                out.push_back({m, m == state.config.model ? "(current)" : "",
                               "/model " + m, true, true});
            }
        }
    } else {
        for (const auto& c : kCommands) {
            if (fuzzy_match(cmd, c.name)) {
                const bool needs_arg = std::string(c.name) == "model";
                out.push_back({"/" + std::string(c.name), c.desc,
                               "/" + std::string(c.name) + (needs_arg ? " " : ""),
                               !needs_arg, false});
            }
        }
    }
    return out;
}

// UTF-8 glyph boundaries (treat each code point as a glyph).
static int glyph_next(const std::string& s, int i) {
    if (i >= static_cast<int>(s.size())) {
        return static_cast<int>(s.size());
    }
    i++;
    while (i < static_cast<int>(s.size()) && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) {
        i++;
    }
    return i;
}

static int glyph_prev(const std::string& s, int i) {
    if (i <= 0) {
        return 0;
    }
    i--;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) {
        i--;
    }
    return i;
}

// Byte offset of glyph column `gcol` within a (wrap-free) string.
static int byte_at_glyph(const std::string& s, int gcol) {
    int b = 0;
    for (int g = 0; g < gcol && b < static_cast<int>(s.size()); g++) {
        b = glyph_next(s, b);
    }
    return b;
}

// Visual (line, glyph-column) of byte position `cursor`, wrapping at `width`
// and honoring explicit '\n'. Must match the wrap used when rendering.
static void cursor_visual(const std::string& s, int cursor, int width, int& line, int& col) {
    if (width < 1) {
        width = 1;
    }
    line = 0;
    col = 0;
    for (int i = 0; i < cursor;) {
        if (s[i] == '\n') {
            line++;
            col = 0;
            i++;
            continue;
        }
        if (col >= width) {
            line++;
            col = 0;
        }
        col++;
        i = glyph_next(s, i);
    }
    if (col >= width) {
        line++;
        col = 0;
    }
}

static int visual_line_count(const std::string& s, int width) {
    if (width < 1) {
        width = 1;
    }
    int lines = 1;
    int col = 0;
    for (int i = 0; i < static_cast<int>(s.size());) {
        if (s[i] == '\n') {
            lines++;
            col = 0;
            i++;
            continue;
        }
        if (col >= width) {
            lines++;
            col = 0;
        }
        col++;
        i = glyph_next(s, i);
    }
    return lines;
}

// Byte position at visual (tline, tcol), clamping tcol to the line's length.
static int byte_at_visual(const std::string& s, int tline, int tcol, int width) {
    if (width < 1) {
        width = 1;
    }
    int line = 0;
    int col = 0;
    int i = 0;
    while (i <= static_cast<int>(s.size())) {
        if (line == tline && col == tcol) {
            return i;
        }
        if (i >= static_cast<int>(s.size())) {
            break;
        }
        if (s[i] == '\n') {
            if (line == tline) {
                return i;  // tcol is past this line: clamp to its end
            }
            line++;
            col = 0;
            i++;
            continue;
        }
        if (col >= width) {
            if (line == tline) {
                return i;
            }
            line++;
            col = 0;
        }
        col++;
        i = glyph_next(s, i);
    }
    return static_cast<int>(s.size());
}

// A wrapping multi-line input that renders a block cursor (FTXUI's built-in
// Input only offers a bar). Reads/writes state.input (which may contain '\n')
// and state.input_cursor (a byte index). Grows up to kMaxLines, then scrolls.
class ChatInputBase : public ComponentBase {
 public:
    ChatInputBase(AppState& state, std::function<void()> on_submit)
        : state_(state), on_submit_(std::move(on_submit)) {}

    bool Focusable() const final { return true; }

    bool OnEvent(Event e) override {
        if (!Focused()) {
            return false;
        }
        std::string& s = state_.input;
        int& cur = state_.input_cursor;
        cur = std::clamp(cur, 0, static_cast<int>(s.size()));

        // Shift+Enter (only if the terminal sends a distinct code) or Alt+Enter
        // insert a newline; plain Enter submits.
        const std::string& in = e.input();
        if (in == "\x1b\n" || in == "\x1b\r" ||      // Alt+Enter
            in == "\x1b[13;2u" || in == "\x1b[27;2;13~") {  // Shift+Enter (CSI-u / xterm)
            s.insert(cur, "\n");
            cur += 1;
            state_.palette_sel = 0;
            return true;
        }
        if (e == Event::Return) {
            on_submit_();
            return true;
        }
        if (e == Event::Backspace) {
            if (cur > 0) {
                const int prev = glyph_prev(s, cur);
                s.erase(prev, cur - prev);
                cur = prev;
                state_.palette_sel = 0;
            }
            return true;
        }
        if (e == Event::Delete) {
            if (cur < static_cast<int>(s.size())) {
                s.erase(cur, glyph_next(s, cur) - cur);
                state_.palette_sel = 0;
            }
            return true;
        }
        if (e == Event::ArrowLeft) {
            if (cur == 0) {
                return false;  // at the start: let focus move to the sidebar
            }
            cur = glyph_prev(s, cur);
            return true;
        }
        if (e == Event::ArrowRight) {
            if (cur >= static_cast<int>(s.size())) {
                return false;  // at the end: let the container handle it
            }
            cur = glyph_next(s, cur);
            return true;
        }
        if (e == Event::ArrowUp || e == Event::ArrowDown) {
            int width = box_.x_max - box_.x_min + 1;
            if (width < 2) {
                width = 60;
            }
            int line, col;
            cursor_visual(s, cur, width, line, col);
            if (e == Event::ArrowUp) {
                if (line == 0) {
                    return false;  // already on the first line
                }
                cur = byte_at_visual(s, line - 1, col, width);
            } else {
                if (line >= visual_line_count(s, width) - 1) {
                    return false;  // already on the last line
                }
                cur = byte_at_visual(s, line + 1, col, width);
            }
            return true;
        }
        if (e == Event::Home) {
            while (cur > 0 && s[cur - 1] != '\n') {
                cur--;
            }
            return true;
        }
        if (e == Event::End) {
            while (cur < static_cast<int>(s.size()) && s[cur] != '\n') {
                cur++;
            }
            return true;
        }
        if (e.is_character()) {
            s.insert(cur, e.character());
            cur += static_cast<int>(e.character().size());
            state_.palette_sel = 0;
            return true;
        }
        return false;
    }

    Element OnRender() override {
        const Theme& t = state_.theme;
        const bool foc = Focused();
        const std::string& s = state_.input;
        int width = box_.x_max - box_.x_min + 1;
        if (width < 2) {
            width = 60;  // fallback before the first reflect
        }

        // Wrap into visual lines (honoring explicit '\n' and soft-wrapping at
        // `width`), and find the cursor's visual line/column with the same rule.
        std::vector<std::string> lines;
        {
            std::string cur;
            int col = 0;
            for (int i = 0; i < static_cast<int>(s.size());) {
                if (s[i] == '\n') {
                    lines.push_back(cur);
                    cur.clear();
                    col = 0;
                    i++;
                    continue;
                }
                if (col >= width) {
                    lines.push_back(cur);
                    cur.clear();
                    col = 0;
                }
                const int nx = glyph_next(s, i);
                cur += s.substr(i, nx - i);
                col++;
                i = nx;
            }
            lines.push_back(cur);
        }
        int cl = 0, cc = 0;
        {
            const int cursor = std::clamp(state_.input_cursor, 0, static_cast<int>(s.size()));
            int col = 0;
            for (int i = 0; i < cursor;) {
                if (s[i] == '\n') {
                    cl++;
                    col = 0;
                    i++;
                    continue;
                }
                if (col >= width) {
                    cl++;
                    col = 0;
                }
                col++;
                i = glyph_next(s, i);
            }
            cc = col;
            if (cc >= width) {  // end of a full line -> start of the next
                cl++;
                cc = 0;
            }
        }
        if (cl >= static_cast<int>(lines.size())) {
            lines.push_back("");
        }

        auto cursor_cell = [&](const std::string& glyph) {
            Element e = text(glyph) | color(t.text);
            // A real terminal block cursor: blinking when focused, steady when not.
            return foc ? (e | focusCursorBlockBlinking) : (e | focusCursorBlock);
        };

        Elements rendered;
        for (int li = 0; li < static_cast<int>(lines.size()); li++) {
            const std::string& ln = lines[li];
            if (li != cl) {
                rendered.push_back(text(ln.empty() ? " " : ln) | color(t.text));
                continue;
            }
            const int cb = byte_at_glyph(ln, cc);
            const bool at_end = cb >= static_cast<int>(ln.size());
            const std::string cg = at_end ? " " : ln.substr(cb, glyph_next(ln, cb) - cb);
            rendered.push_back(hbox({
                text(ln.substr(0, cb)) | color(t.text),
                cursor_cell(cg),
                text(at_end ? "" : ln.substr(glyph_next(ln, cb))) | color(t.text),
            }));
        }

        Element doc = vbox(std::move(rendered));
        if (static_cast<int>(lines.size()) > kMaxLines) {
            doc = doc | yframe | vscroll_indicator | size(HEIGHT, EQUAL, kMaxLines);
        }
        doc = doc | reflect(box_);
        return doc;
    }

 private:
    static constexpr int kMaxLines = 15;

    AppState& state_;
    std::function<void()> on_submit_;
    Box box_;  // captured via reflect to know the wrap width
};

static Component ChatInput(AppState& state, std::function<void()> on_submit) {
    return Make<ChatInputBase>(state, std::move(on_submit));
}

static Component build_chat_view(AppState& state, ScreenInteractive& screen) {
    // Submit is only reached when the palette is closed (the root handler
    // intercepts Enter while it's open): run a "/command" or send a message.
    auto submit = [&state, &screen] {
        if (!state.input.empty() && state.input.front() == '/') {
            run_slash(state, screen, state.input);
            state.input.clear();
            state.input_cursor = 0;
            return;
        }
        send_message(state, screen);
    };
    auto input = ChatInput(state, submit);

    auto history = Renderer([&state] {
        const Theme& t = state.theme;
        const Conversation& conv = state.active();
        Elements items;
        if (conv.messages.empty()) {
            items.push_back(filler());
            items.push_back(text("Nothing here yet - say something.") | color(t.text_dim) | center);
            items.push_back(filler());
        } else {
            for (const auto& m : conv.messages) {
                items.push_back(render_message(t, m));
                items.push_back(text(""));
            }
        }
        // scroll is 0=top .. 1=bottom; 1 follows the latest message. The frame
        // maps that fraction to a clamped scroll offset.
        return vbox(std::move(items))
            | focusPositionRelative(0, conv.scroll) | yframe | vscroll_indicator | flex;
    });

    auto container = Container::Vertical({history, input});
    container->SetActiveChild(input);
    auto view = Renderer(container, [history, input, &state] {
        const Theme& t = state.theme;
        const Conversation& conv = state.active();
        auto info = hbox({
            text("   " + state.config.model) | color(t.model) | bold,
            text("   session ") | color(t.text_dim),
            text(std::to_string(conv.total_in)) | color(t.token_in),
            text(" / ") | color(t.text_dim),
            text(std::to_string(conv.total_out)) | color(t.token_out),
            text("   total ") | color(t.text_dim),
            text(std::to_string(conv.total_in + conv.total_out)) | color(t.text),
            filler(),
        });  // bg inherited from the (focus-aware) content area

        // Slash palette above the input: matching commands, or installed models
        // once the command is "/model". The highlighted row (palette_sel) is what
        // arrow keys move and Tab/Enter autofill.
        Elements palette;
        const auto pitems = compute_palette(state);
        if (!pitems.empty()) {
            const int sel = std::clamp(state.palette_sel, 0, static_cast<int>(pitems.size()) - 1);
            for (int i = 0; i < static_cast<int>(pitems.size()); ++i) {
                const auto& it = pitems[i];
                const bool on = (i == sel);
                Elements row{
                    text(on ? " ▶ " : "   ") | color(t.accent),
                    text(it.label) | color(it.is_model ? t.model : t.accent) | bold,
                };
                if (!it.desc.empty()) {
                    row.push_back(text("   " + it.desc) | color(t.text_dim));
                }
                row.push_back(filler());
                palette.push_back(hbox(std::move(row)) | bgcolor(on ? t.panel_alt : t.panel));
            }
        } else if (state.input.rfind("/model", 0) == 0 && state.models.empty()) {
            palette.push_back(text("  (no models found - set the host in Settings)")
                              | color(t.text_dim) | bgcolor(t.panel));
        }

        Elements col;
        col.push_back(history->Render() | flex);
        if (!palette.empty()) {
            col.push_back(vbox(std::move(palette)));
        }
        col.push_back(hbox({
                          text(state.streaming ? " ◐ " : " ❯ ") | color(t.accent) | bold,
                          input->Render() | flex,
                      }) | bgcolor(t.panel_alt));
        col.push_back(info);
        col.push_back(text(""));
        return vbox(std::move(col));
    });

    // Scroll the history: mouse wheel, or PageUp/PageDown. scroll==1 follows the
    // bottom. Wheel steps are smaller than a page.
    return CatchEvent(view, [&state](Event e) {
        if (e == Event::PageUp) {
            state.active().scroll = std::max(0.0f, state.active().scroll - 0.25f);
            return true;
        }
        if (e == Event::PageDown) {
            state.active().scroll = std::min(1.0f, state.active().scroll + 0.25f);
            return true;
        }
        if (e.is_mouse()) {
            if (e.mouse().button == Mouse::Button::WheelUp) {
                state.active().scroll = std::max(0.0f, state.active().scroll - 0.08f);
                return true;
            }
            if (e.mouse().button == Mouse::Button::WheelDown) {
                state.active().scroll = std::min(1.0f, state.active().scroll + 0.08f);
                return true;
            }
        }
        return false;
    });
}

static Component build_settings_view(AppState& state, ScreenInteractive& screen) {
    InputOption line;
    line.multiline = false;
    line.transform = plain_input(state.theme.text);
    auto host_input = Input(&state.config.host, "http://localhost:11434", line);

    // Save highlights from our focus index (not its own focus), since it doubles
    // as the active child for the virtual archived rows.
    ButtonOption bopt;
    bopt.transform = [&state](const EntryState&) {
        const Theme& t = state.theme;
        Element e = text(" Save ");
        return (state.settings_sel == 1) ? (e | color(t.bg) | bgcolor(t.accent) | bold)
                                         : (e | color(t.accent) | bgcolor(t.panel_alt));
    };
    auto save = Button(
        "Save",
        [&state, &screen] {
            state.status = save_config(state.config) ? "settings saved" : "could not save settings";
            refresh_models(state, screen);  // the host may have changed
        },
        bopt);

    auto container = Container::Vertical({host_input, save});

    // Focus rows: 0 host, 1 Save, then (if any) 2 = Export all, 3+i = archived[i].
    auto nfoc = [&state] {
        return state.archived.empty() ? 2 : 3 + static_cast<int>(state.archived.size());
    };

    auto view = Renderer(container, [host_input, save, &state, nfoc] {
        const Theme& t = state.theme;
        if (state.settings_sel >= nfoc()) {
            state.settings_sel = 0;
        }
        auto mark = [&](int i) { return (state.settings_sel == i) ? std::string(" ▶ ") : std::string("   "); };

        Elements rows;
        rows.push_back(text(""));
        rows.push_back(text("  Settings") | color(t.accent) | bold);
        rows.push_back(text(""));
        rows.push_back(hbox({
            text(mark(0) + "Ollama host  ") | (state.settings_sel == 0 ? (color(t.accent) | bold) : color(t.text_dim)),
            hbox({text(" "), host_input->Render() | flex}) | bgcolor(t.panel_alt),
        }));
        rows.push_back(text(""));
        rows.push_back(hbox({text(mark(1)), save->Render()}));
        rows.push_back(text(""));
        rows.push_back(hbox({
            text("   model in use  ") | color(t.text_dim),
            text(state.config.model) | color(t.model) | bold,
        }));
        rows.push_back(text(""));
        rows.push_back(text("  Archived chats") | color(t.accent) | bold);
        rows.push_back(text(""));
        if (state.archived.empty()) {
            rows.push_back(text("   (none)") | color(t.text_dim));
        } else {
            rows.push_back(hbox({
                text(mark(2) + "Export all") | (state.settings_sel == 2 ? (color(t.accent) | bold) : color(t.text)),
                text("  (" + std::to_string(state.archived.size()) + ")") | color(t.text_dim),
            }));
            for (int i = 0; i < static_cast<int>(state.archived.size()); ++i) {
                std::string title = state.archived[i].title.empty() ? "Untitled" : state.archived[i].title;
                if (title.size() > 30) {
                    title = title.substr(0, 29) + "…";
                }
                rows.push_back(text(mark(3 + i) + "├ " + title)
                               | (state.settings_sel == 3 + i ? (color(t.select) | bold) : color(t.text)));
            }
        }
        Element doc = vbox(std::move(rows)) | flex;
        // Hide the terminal cursor unless the host field is being edited, so it
        // doesn't park in the corner on the Save / archived rows.
        return host_input->Focused() ? doc : (doc | focus);
    });

    return CatchEvent(view, [host_input, save, container, nfoc, &state](Event e) {
        if (e == Event::ArrowDown) {
            state.settings_sel = (state.settings_sel + 1) % nfoc();
            container->SetActiveChild(state.settings_sel == 0 ? host_input : save);
            return true;
        }
        if (e == Event::ArrowUp) {
            state.settings_sel = (state.settings_sel + nfoc() - 1) % nfoc();
            container->SetActiveChild(state.settings_sel == 0 ? host_input : save);
            return true;
        }
        if (e == Event::Return) {
            if (state.settings_sel == 2 && !state.archived.empty()) {
                int count = 0;
                for (const auto& c : state.archived) {
                    if (!storage::export_markdown(c).empty()) {
                        count++;
                    }
                }
                state.status = "exported " + std::to_string(count) + " chats to " + storage::export_dir();
                return true;
            }
            if (state.settings_sel >= 3) {  // an archived chat -> manage popup
                const int i = state.settings_sel - 3;
                if (i < static_cast<int>(state.archived.size())) {
                    state.popup = 2;
                    state.popup_conv = i;
                    state.popup_sel = 0;  // default to Export (non-destructive)
                }
                return true;
            }
        }
        return false;  // host / Save handle their own keys
    });
}

// A small tree sidebar: "New Chat" with the saved conversations nested under it,
// then "Settings". Navigating it live-displays whatever row is hovered.
class SidebarBase : public ComponentBase {
 public:
    explicit SidebarBase(AppState& state) : state_(state) {}

    bool Focusable() const final { return true; }

    bool OnEvent(Event e) override {
        if (!Focused()) {
            return false;
        }
        if (e == Event::ArrowUp) {
            move(-1);
            return true;
        }
        if (e == Event::ArrowDown) {
            move(1);
            return true;
        }
        if (e == Event::Return) {
            // Enter opens the action menu for a hovered conversation. (Entering a
            // view is the Right arrow, handled by the parent Horizontal container.)
            const int n = static_cast<int>(state_.conversations.size());
            if (state_.sidebar_sel >= 1 && state_.sidebar_sel <= n) {
                state_.popup = 1;
                state_.popup_conv = state_.sidebar_sel - 1;
                state_.popup_sel = 3;  // default to the safe option (Cancel)
                return true;
            }
            return false;
        }
        return false;
    }

    Element OnRender() override {
        const Theme& t = state_.theme;
        const bool foc = Focused();
        const int n = static_cast<int>(state_.conversations.size());

        Elements rows;
        rows.push_back(text("  VIEWS") | color(foc ? t.accent : t.text_dim) | bold);
        rows.push_back(text(""));
        rows.push_back(make_row(0, "", "New Chat", t.text, foc));
        for (int i = 0; i < n; ++i) {
            const std::string& title = state_.conversations[i].title;
            std::string label = title.empty() ? "New chat" : title;
            if (label.size() > 14) {
                label = label.substr(0, 13) + "…";
            }
            const std::string branch = (i == n - 1) ? "└ " : "├ ";
            rows.push_back(make_row(i + 1, branch, label, t.text, foc));
        }
        rows.push_back(make_row(n + 1, "", "Settings", t.text_dim, foc));
        return vbox(std::move(rows));
    }

 private:
    int last_row() const { return static_cast<int>(state_.conversations.size()) + 1; }

    // Move the highlight and live-display the newly hovered row.
    void move(int delta) {
        state_.sidebar_sel = std::clamp(state_.sidebar_sel + delta, 0, last_row());
        const int n = static_cast<int>(state_.conversations.size());
        const int s = state_.sidebar_sel;
        if (s == 0) {
            state_.active_conv = -1;  // blank draft
            state_.view = 0;
        } else if (s <= n) {
            state_.active_conv = s - 1;
            state_.view = 0;
        } else {
            state_.view = 1;  // Settings
        }
    }

    Element make_row(int idx, const std::string& branch, const std::string& label, Color base, bool foc) {
        const Theme& t = state_.theme;
        const bool on = (state_.sidebar_sel == idx);
        Element e = text((on ? "▶ " : "  ") + branch + label) | color(on ? t.select : base);
        if (on) {
            e |= bold;
        }
        if (on && foc) {
            e |= bgcolor(t.panel_alt);
        }
        return e;
    }

    AppState& state_;
};

static Component Sidebar(AppState& state) {
    return Make<SidebarBase>(state);
}

// Truly delete the active conversation at `idx`, then return to a blank draft.
static void delete_conv(AppState& state, int idx) {
    if (idx < 0 || idx >= static_cast<int>(state.conversations.size())) {
        return;
    }
    storage::erase(state.conversations[idx].id);
    state.conversations.erase(state.conversations.begin() + idx);
    state.active_conv = -1;
    state.sidebar_sel = 0;
    state.draft = Conversation{};
}

// Permanently delete an archived chat (file + list entry).
static void delete_archived(AppState& state, int idx) {
    if (idx < 0 || idx >= static_cast<int>(state.archived.size())) {
        return;
    }
    storage::erase(state.archived[idx].id);
    state.archived.erase(state.archived.begin() + idx);
}

// Move a conversation out of the tree into the archived list (and on disk).
static void archive_conv(AppState& state, int idx) {
    if (idx < 0 || idx >= static_cast<int>(state.conversations.size())) {
        return;
    }
    Conversation c = state.conversations[idx];
    state.conversations.erase(state.conversations.begin() + idx);
    storage::erase(c.id);      // remove from conversations/
    storage::save(c, true);    // write to archived/
    state.archived.push_back(std::move(c));
    state.active_conv = -1;
    state.sidebar_sel = 0;
    state.draft = Conversation{};
}

// The centered action modal. type 1 = active chat (Delete/Archive/Rename/Cancel),
// type 2 = archived chat (Export/Delete/Rename/Cancel).
static Element confirm_popup(const Theme& t, int type, const std::string& title, int sel) {
    auto btn = [&](const std::string& label, int idx, Color c) {
        Element e = text(" " + label + " ");
        return (sel == idx) ? (e | color(t.bg) | bgcolor(c) | bold)
                            : (e | color(c) | bgcolor(t.panel_alt));
    };
    const std::string disp = title.empty() ? "this chat" : ("\"" + title + "\"");
    Elements buttons{filler()};
    if (type == 1) {
        buttons.push_back(btn("Delete", 0, t.err));
        buttons.push_back(text("  "));
        buttons.push_back(btn("Archive", 1, t.select));
    } else {
        buttons.push_back(btn("Export", 0, t.select));
        buttons.push_back(text("  "));
        buttons.push_back(btn("Delete", 1, t.err));
    }
    buttons.push_back(text("  "));
    buttons.push_back(btn("Rename", 2, t.accent));
    buttons.push_back(text("  "));
    buttons.push_back(btn("Cancel", 3, t.text_dim));
    buttons.push_back(filler());
    return vbox({
               text(type == 1 ? "Manage chat" : "Archived chat") | bold | color(t.accent) | hcenter,
               text(""),
               paragraph(disp) | color(t.text) | hcenter,
               text(""),
               hbox(std::move(buttons)),
           }) |
           border | bgcolor(t.panel) | size(WIDTH, GREATER_THAN, 42);
}

// The rename modal: a single-line field with a block cursor over the title.
static Element rename_popup(const Theme& t, const std::string& buf, int cursor) {
    const int cur = std::clamp(cursor, 0, static_cast<int>(buf.size()));
    const bool at_end = cur >= static_cast<int>(buf.size());
    const std::string cg = at_end ? " " : buf.substr(cur, glyph_next(buf, cur) - cur);
    auto field = hbox({
                     text(buf.substr(0, cur)) | color(t.text),
                     text(cg) | color(t.text) | focusCursorBlockBlinking,
                     text(at_end ? "" : buf.substr(glyph_next(buf, cur))) | color(t.text),
                     filler(),
                 }) | bgcolor(t.panel_alt);
    return vbox({
               text("Rename chat") | bold | color(t.accent) | hcenter,
               text(""),
               hbox({text(" "), field, text(" ")}),
               text(""),
               text("Enter: save    Esc: cancel") | color(t.text_dim) | hcenter,
           }) |
           border | bgcolor(t.panel) | size(WIDTH, GREATER_THAN, 42);
}

// Open the rename popup (type 3 = active chat, 4 = archived) seeded with the
// current title, with the cursor at the end.
static void begin_rename(AppState& state, int type, int idx) {
    const auto& list = (type == 4) ? state.archived : state.conversations;
    if (idx < 0 || idx >= static_cast<int>(list.size())) {
        return;
    }
    state.rename_buf = list[idx].title;
    state.rename_cursor = static_cast<int>(state.rename_buf.size());
    state.popup = type;
    state.popup_conv = idx;
}

// Commit the rename popup: write the edited title back and persist.
static void commit_rename(AppState& state) {
    const bool archived = (state.popup == 4);
    auto& list = archived ? state.archived : state.conversations;
    const int idx = state.popup_conv;
    if (idx >= 0 && idx < static_cast<int>(list.size())) {
        list[idx].title = state.rename_buf;
        storage::save(list[idx], archived);
        state.status = "chat renamed";
    }
    state.popup = 0;
}

// Drop a finished bracketed paste into whichever text field is active: the
// rename editor (flattened to a single line) or, otherwise, the chat input.
static void insert_paste(AppState& state, const std::string& text) {
    if (text.empty()) {
        return;
    }
    if (state.popup == 3 || state.popup == 4) {
        std::string flat = text;
        std::replace(flat.begin(), flat.end(), '\n', ' ');
        std::replace(flat.begin(), flat.end(), '\t', ' ');
        int& cur = state.rename_cursor;
        cur = std::clamp(cur, 0, static_cast<int>(state.rename_buf.size()));
        state.rename_buf.insert(cur, flat);
        cur += static_cast<int>(flat.size());
        return;
    }
    if (state.view != 0) {
        return;  // no text field to paste into outside the chat view
    }
    int& cur = state.input_cursor;
    cur = std::clamp(cur, 0, static_cast<int>(state.input.size()));
    state.input.insert(cur, text);
    cur += static_cast<int>(text.size());
    state.palette_sel = 0;
}

ftxui::Component build_app(AppState& state, ScreenInteractive& screen) {
    refresh_models(state, screen);  // populate the model list on startup

    auto chat_view = build_chat_view(state, screen);
    auto settings_view = build_settings_view(state, screen);
    auto content = Container::Tab({chat_view, settings_view}, &state.view);

    auto sidebar = Sidebar(state);
    auto body = Container::Horizontal({sidebar, content});
    body->SetActiveChild(content);

    auto layout = Renderer(body, [sidebar, content, &state] {
        const Theme& th = state.theme;
        // Brighten whichever panel currently has focus, so it's clear where you
        // are when moving between the sidebar and the view.
        const bool side_foc = sidebar->Focused();

        auto header = hbox({
            text(" ▌ ") | color(th.accent) | bold,
            text("Hearth") | color(th.accent) | bold,
            filler(),
        }) | bgcolor(th.panel);

        auto sidebar_panel = vbox({
            text(""),
            sidebar->Render(),
            filler(),
        }) | size(WIDTH, EQUAL, 20) | bgcolor(side_foc ? th.panel_focus : th.panel);

        // Context-aware key hints: surface the keys relevant to what's focused.
        std::string hint;
        if (state.popup == 3 || state.popup == 4) {
            hint = "Type to rename   Enter: save   Esc: cancel";
        } else if (state.popup) {
            hint = "←/→: choose   Enter: confirm   Esc: cancel";
        } else if (side_foc) {
            const int nconv = static_cast<int>(state.conversations.size());
            hint = (state.sidebar_sel >= 1 && state.sidebar_sel <= nconv)
                       ? "↑/↓: navigate   →: enter view   Enter: delete / archive   Ctrl+C: quit"
                       : "↑/↓: navigate   →: enter view   Ctrl+C: quit";
        } else if (state.view == 1) {
            if (state.settings_sel >= 3) {
                hint = "←: views   ↑/↓: move   Enter: manage chat   Ctrl+C: quit";
            } else if (state.settings_sel == 2) {
                hint = "←: views   ↑/↓: move   Enter: export all   Ctrl+C: quit";
            } else {
                hint = "←: views   ↑/↓: move   Enter: save   Ctrl+C: quit";
            }
        } else {
            hint = "←: views   / commands   Ctrl+C: quit";
        }

        // Two-line footer: key hints on top; the status indicator (with any
        // notification pushed to the right) on the bottom.
        auto hint_line = hbox({
            text("  " + hint + " ") | color(th.text_dim),
            filler(),
        }) | bgcolor(th.panel);
        auto status_line = hbox({
            text(state.streaming ? " ◐ streaming " : " ● ready ")
                | color(state.streaming ? th.warn : th.ok),
            filler(),
            text(state.status.empty() ? "" : (state.status + " "))
                | color(state.status.rfind("error", 0) == 0 ? th.err : th.ok) | bold,
        }) | bgcolor(th.panel);
        auto statusbar = vbox({hint_line, status_line});

        Element doc = vbox({
            header,
            hbox({
                sidebar_panel,
                content->Render() | flex | bgcolor(side_foc ? th.bg : th.bg_focus),
            }) | flex,
            statusbar,
        }) | bgcolor(th.bg);

        if (state.popup == 3 || state.popup == 4) {
            doc = dbox({doc, rename_popup(th, state.rename_buf, state.rename_cursor) | center});
        } else if (state.popup) {
            std::string title;
            if (state.popup == 1 && state.popup_conv >= 0 &&
                state.popup_conv < static_cast<int>(state.conversations.size())) {
                title = state.conversations[state.popup_conv].title;
            } else if (state.popup == 2 && state.popup_conv >= 0 &&
                       state.popup_conv < static_cast<int>(state.archived.size())) {
                title = state.archived[state.popup_conv].title;
            }
            doc = dbox({doc, confirm_popup(th, state.popup, title, state.popup_sel) | center});
        }
        return doc;
    });

    // Ctrl+C quits: a raw byte here (raw mode), and via FTXUI's own SIGINT
    // handler in a real tty. Arrow keys navigate (Left/Right move between sidebar
    // and view, Up/Down within). When the slash palette is open it captures the
    // arrows + Tab/Enter for completion; otherwise Tab is swallowed (reserved).
    return CatchEvent(layout, [sidebar, &state, &screen](Event e) {
        probe_extended_keys(state);  // first event: query terminal support (unless forced)

        // Bracketed paste: the terminal frames a paste in ESC[200~ ... ESC[201~.
        // We gather everything between verbatim so a multi-line paste lands as
        // text in the input instead of submitting at each embedded newline.
        if (e.input() == "\x1b[200~") {
            state.pasting = true;
            state.paste_buf.clear();
            return true;
        }
        if (state.pasting) {
            if (e.input() == "\x1b[201~") {
                state.pasting = false;
                insert_paste(state, state.paste_buf);
                return true;
            }
            if (e.is_character()) {
                state.paste_buf += e.character();
            } else if (e.input() == "\n" || e.input() == "\r") {
                state.paste_buf += "\n";
            } else if (e.input() == "\t") {
                state.paste_buf += "\t";
            }
            return true;  // swallow the rest of the paste payload
        }

        // Finishing a mouse drag copies the selection to the clipboard (OSC 52),
        // so "select + Ctrl+Shift+V elsewhere" works. Not consumed: FTXUI still
        // owns the drag for highlighting the selection.
        if (e.is_mouse() && e.mouse().button == Mouse::Left &&
            e.mouse().motion == Mouse::Released) {
            const std::string sel = screen.GetSelection();
            if (!sel.empty()) {
                copy_to_clipboard(sel);
                state.status = "copied selection to clipboard";
            }
        }

        if (!state.extended_keys && is_extended_keys_reply(e.input())) {
            std::cout << "\x1b[>1u" << std::flush;  // supported: push the flag, turn on
            state.extended_keys = true;
            return true;  // consume the capability report
        }
        if (state.extended_keys && !e.is_mouse() && !e.is_cursor_position() &&
            translate_key(e.input(), state, screen)) {
            return true;  // a CSI-u key report we remapped to a legacy event
        }
        if (e.input() == std::string(1, '\x03')) {
            restore_terminal(state);
            screen.ExitLoopClosure()();
            return true;
        }
        // The rename popup is a modal single-line editor over the title.
        if (state.popup == 3 || state.popup == 4) {
            std::string& s = state.rename_buf;
            int& cur = state.rename_cursor;
            cur = std::clamp(cur, 0, static_cast<int>(s.size()));
            if (e == Event::Return) {
                commit_rename(state);
            } else if (e == Event::Escape) {
                state.popup = 0;
            } else if (e == Event::Backspace) {
                if (cur > 0) {
                    const int prev = glyph_prev(s, cur);
                    s.erase(prev, cur - prev);
                    cur = prev;
                }
            } else if (e == Event::Delete) {
                if (cur < static_cast<int>(s.size())) {
                    s.erase(cur, glyph_next(s, cur) - cur);
                }
            } else if (e == Event::ArrowLeft) {
                cur = glyph_prev(s, cur);
            } else if (e == Event::ArrowRight) {
                cur = glyph_next(s, cur);
            } else if (e == Event::Home) {
                cur = 0;
            } else if (e == Event::End) {
                cur = static_cast<int>(s.size());
            } else if (e.is_character()) {
                s.insert(cur, e.character());
                cur += static_cast<int>(e.character().size());
            }
            return true;  // modal: swallow everything else
        }
        // The action menu is modal: it captures all input while open.
        if (state.popup) {
            if (e == Event::ArrowLeft) {
                state.popup_sel = std::max(0, state.popup_sel - 1);
            } else if (e == Event::ArrowRight) {
                state.popup_sel = std::min(3, state.popup_sel + 1);
            } else if (e == Event::Escape) {
                state.popup = 0;
            } else if (e == Event::Return) {
                const int act = state.popup_sel;
                const int idx = state.popup_conv;
                const int type = state.popup;
                if (act == 2) {  // Rename: open the editor (type 1->3, 2->4)
                    begin_rename(state, type + 2, idx);
                    return true;
                }
                state.popup = 0;
                if (type == 1) {
                    if (act == 0) {
                        delete_conv(state, idx);
                    } else if (act == 1) {
                        archive_conv(state, idx);
                    }
                } else if (type == 2) {
                    if (act == 0 && idx >= 0 && idx < static_cast<int>(state.archived.size())) {
                        const std::string p = storage::export_markdown(state.archived[idx]);
                        state.status = p.empty() ? "export failed" : ("exported to " + p);
                    } else if (act == 1) {
                        delete_archived(state, idx);
                    }
                }
            }
            return true;
        }
        // Palette nav only when the chat input (not the sidebar) is focused.
        if (state.view == 0 && !sidebar->Focused()) {
            const auto pitems = compute_palette(state);
            if (!pitems.empty()) {
                const int n = static_cast<int>(pitems.size());
                const int sel = std::clamp(state.palette_sel, 0, n - 1);
                if (e == Event::ArrowUp) {
                    state.palette_sel = (sel - 1 + n) % n;
                    return true;
                }
                if (e == Event::ArrowDown) {
                    state.palette_sel = (sel + 1) % n;
                    return true;
                }
                if (e == Event::Tab || e == Event::Return) {
                    state.input = pitems[sel].fill;
                    state.input_cursor = static_cast<int>(state.input.size());
                    state.palette_sel = 0;
                    if (e == Event::Return && pitems[sel].run) {
                        run_slash(state, screen, state.input);
                        state.input.clear();
                        state.input_cursor = 0;
                    }
                    return true;
                }
            }
        }
        if (e == Event::Tab || e == Event::TabReverse) {
            return true;  // reserved otherwise
        }
        return false;
    });
}
