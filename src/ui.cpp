#include "ui.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <cctype>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "config.h"
#include "markdown.h"
#include "ollama.h"
#include "theme.h"

using namespace ftxui;

static const std::vector<std::string> kViews = {"Chat", "Settings"};

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

// Claude-Code style: the user's turn is a highlighted block; everything else is
// the model and renders as Markdown with no "You/Assistant" labels. System
// notices (slash-command output) render dimmed.
static Element render_message(const Theme& t, const Message& m) {
    if (m.role == "user") {
        return hbox({
            text(" ❯ ") | color(t.accent) | bold,
            paragraph(m.content) | color(t.user_fg) | bold | flex,
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

    state.messages.push_back({"user", state.input});
    state.messages.push_back({"assistant", ""}); // placeholder we stream into
    state.input.clear();
    state.streaming = true;
    state.scroll = 1.0f;  // jump to the bottom to follow the reply
    state.status.clear(); // the streaming indicator conveys progress

    std::vector<ollama::ChatMessage> request;
    for (size_t i = 0; i + 1 < state.messages.size(); ++i) {
        request.push_back({state.messages[i].role, state.messages[i].content});
    }
    const std::string host = state.config.host;
    const std::string model = state.config.model;

    std::thread([&state, &screen, request, host, model] {
        ollama::chat_stream(
            host, model, request,
            [&state, &screen](const std::string& tok) {
                screen.Post([&state, tok] {
                    if (!state.messages.empty()) {
                        state.messages.back().content += tok;
                    }
                });
            },
            [&state, &screen](bool ok, std::string err, ollama::ChatStats stats) {
                screen.Post([&state, ok, err, stats] {
                    state.streaming = false;
                    state.status = ok ? "" : ("error: " + err);
                    if (ok) {
                        state.last_in = stats.prompt_tokens;
                        state.last_out = stats.eval_tokens;
                        state.total_in += stats.prompt_tokens;
                        state.total_out += stats.eval_tokens;
                    } else if (!state.messages.empty() && state.messages.back().content.empty()) {
                        state.messages.back().content = "(no response)";
                    }
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
    {"clear", "clear the conversation"},
    {"model", "pick a model (lists installed)"},
    {"quit", "exit Hearth"},
};

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

    state.scroll = 1.0f;
    if (cmd == "clear") {
        state.messages.clear();
        state.last_in = state.last_out = 0;
        state.total_in = state.total_out = 0;
        state.status = "chat cleared";
    } else if (cmd == "help") {
        std::string txt = "Commands:";
        for (const auto& c : kCommands) {
            txt += "\n  /" + std::string(c.name) + "   " + c.desc;
        }
        state.messages.push_back({"system", txt});
    } else if (cmd == "model") {
        if (args.empty()) {
            if (state.models.empty()) {
                state.status = "no models found - check the host in Settings";
            } else {
                std::string txt = "Installed models:";
                for (const auto& m : state.models) {
                    txt += "\n  " + m + (m == state.config.model ? "  (current)" : "");
                }
                state.messages.push_back({"system", txt});
            }
        } else {
            const std::string chosen = resolve_model(state, args);
            state.config.model = chosen;
            save_config(state.config);  // remember the choice across restarts
            state.status = "model set to " + chosen;
        }
    } else if (cmd == "quit" || cmd == "exit") {
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

static Component build_chat_view(AppState& state, ScreenInteractive& screen) {
    InputOption opt;
    opt.multiline = false;
    opt.cursor_position = &state.input_cursor;
    opt.on_change = [&state] { state.palette_sel = 0; };
    opt.on_enter = [&state, &screen] {
        // Only reached when the palette is closed (the root handler intercepts
        // Enter while it's open). Run a "/command" or send a chat message.
        if (!state.input.empty() && state.input.front() == '/') {
            run_slash(state, screen, state.input);
            state.input.clear();
            state.input_cursor = 0;
            return;
        }
        send_message(state, screen);
    };
    opt.transform = plain_input(state.theme.text);
    auto input = Input(&state.input, "Type a message…   ( / for commands )", opt);

    auto history = Renderer([&state] {
        const Theme& t = state.theme;
        Elements items;
        if (state.messages.empty()) {
            items.push_back(filler());
            items.push_back(text("Nothing here yet - say something.") | color(t.text_dim) | center);
            items.push_back(filler());
        } else {
            for (const auto& m : state.messages) {
                items.push_back(render_message(t, m));
                items.push_back(text(""));
            }
        }
        // scroll is 0=top .. 1=bottom; 1 follows the latest message. The frame
        // maps that fraction to a clamped scroll offset.
        return vbox(std::move(items))
            | focusPositionRelative(0, state.scroll) | yframe | vscroll_indicator | flex;
    });

    auto container = Container::Vertical({history, input});
    container->SetActiveChild(input);
    auto view = Renderer(container, [history, input, &state] {
        const Theme& t = state.theme;
        auto info = hbox({
            text("   " + state.config.model) | color(t.model) | bold,
            text("   session ") | color(t.text_dim),
            text(std::to_string(state.total_in)) | color(t.token_in),
            text(" / ") | color(t.text_dim),
            text(std::to_string(state.total_out)) | color(t.token_out),
            text("   total ") | color(t.text_dim),
            text(std::to_string(state.total_in + state.total_out)) | color(t.text),
            filler(),
        }) | bgcolor(t.bg);

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
        col.push_back(text("") | bgcolor(t.bg));
        return vbox(std::move(col));
    });

    // Scroll the history: mouse wheel, or PageUp/PageDown. scroll==1 follows the
    // bottom. Wheel steps are smaller than a page.
    return CatchEvent(view, [&state](Event e) {
        if (e == Event::PageUp) {
            state.scroll = std::max(0.0f, state.scroll - 0.25f);
            return true;
        }
        if (e == Event::PageDown) {
            state.scroll = std::min(1.0f, state.scroll + 0.25f);
            return true;
        }
        if (e.is_mouse()) {
            if (e.mouse().button == Mouse::Button::WheelUp) {
                state.scroll = std::max(0.0f, state.scroll - 0.08f);
                return true;
            }
            if (e.mouse().button == Mouse::Button::WheelDown) {
                state.scroll = std::min(1.0f, state.scroll + 0.08f);
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
    auto save = Button("  Save  ", [&state, &screen] {
        state.status = save_config(state.config) ? "settings saved" : "could not save settings";
        refresh_models(state, screen);  // the host may have changed
    });

    auto container = Container::Vertical({host_input, save});
    auto focus = std::make_shared<int>(0);

    auto view = Renderer(container, [host_input, save, focus, &state] {
        const Theme& t = state.theme;
        auto field = [&](const std::string& label, Component c, int i) {
            const bool f = (*focus == i);
            return hbox({
                text((f ? " ▶ " : "   ") + label) | (f ? (color(t.accent) | bold) : color(t.text_dim)),
                hbox({text(" "), c->Render() | flex}) | bgcolor(t.panel_alt),
            });
        };
        return vbox({
            text(""),
            text("  Settings") | color(t.accent) | bold,
            text(""),
            field("Ollama host  ", host_input, 0),
            text(""),
            hbox({text("   "), save->Render()}),
            text(""),
            hbox({
                text("   model in use  ") | color(t.text_dim),
                text(state.config.model) | color(t.model) | bold,
            }),
            text(""),
            hbox({
                text("   "),
                paragraph("Up/Down to move, Enter to save. Pick a model in chat with "
                          "/model - it lists what's installed on the server above.")
                    | color(t.text_dim),
            }),
        }) | flex | bgcolor(t.bg);
    });

    // Single-line fields would otherwise swallow Up/Down, so we drive focus
    // between the two fields and the Save button ourselves.
    return CatchEvent(view, [container, focus](Event e) {
        const int n = static_cast<int>(container->ChildCount());
        if (e == Event::ArrowDown) {
            *focus = (*focus + 1) % n;
        } else if (e == Event::ArrowUp) {
            *focus = (*focus + n - 1) % n;
        } else {
            return false;
        }
        container->SetActiveChild(container->ChildAt(*focus));
        return true;
    });
}

ftxui::Component build_app(AppState& state, ScreenInteractive& screen) {
    refresh_models(state, screen);  // populate the model list on startup

    auto chat_view = build_chat_view(state, screen);
    auto settings_view = build_settings_view(state, screen);
    auto content = Container::Tab({chat_view, settings_view}, &state.tab_index);

    const Theme t = state.theme;
    MenuOption menu_opt = MenuOption::Vertical();
    menu_opt.entries_option.transform = [t](const EntryState& s) {
        std::string label = (s.active ? " ▶ " : "   ") + s.label;
        label.resize(18, ' '); // pad to fill the column width
        Element e = text(label);
        if (s.active) {
            e |= color(t.select) | bold | bgcolor(t.panel_alt);
        } else {
            e |= color(t.text_dim);
        }
        if (s.focused) {
            e |= bgcolor(t.user_bg);
        }
        return e;
    };
    auto menu = Menu(&kViews, &state.tab_index, menu_opt);

    auto body = Container::Horizontal({menu, content});
    body->SetActiveChild(content);

    auto layout = Renderer(body, [menu, content, &state] {
        const Theme& th = state.theme;
        const bool menu_focused = menu->Focused();

        auto header = hbox({
            text(" ▌ ") | color(th.accent) | bold,
            text("Hearth") | color(th.accent) | bold,
            filler(),
        }) | bgcolor(th.panel);

        auto sidebar = vbox({
            text(""),
            text("  VIEWS") | color(menu_focused ? th.accent : th.text_dim) | bold,
            text(""),
            menu->Render(),
            filler(),
        }) | size(WIDTH, EQUAL, 18) | bgcolor(th.panel);

        auto statusbar = hbox({
            text(state.streaming ? " ◐ streaming " : " ● ready ")
                | color(state.streaming ? th.warn : th.ok),
            text("  ↑/↓ ←/→: navigate   Ctrl+C: quit ")
                | color(th.text_dim),
            filler(),
            text(state.status.empty() ? "" : (state.status + " "))
                | color(state.status.rfind("error", 0) == 0 ? th.err : th.ok) | bold,
        }) | bgcolor(th.panel);

        return vbox({
            header,
            hbox({
                sidebar,
                content->Render() | flex | bgcolor(th.bg),
            }) | flex,
            statusbar,
        }) | bgcolor(th.bg);
    });

    // Ctrl+C quits: a raw byte here (raw mode), and via FTXUI's own SIGINT
    // handler in a real tty. Arrow keys navigate (Left/Right move between sidebar
    // and view, Up/Down within). When the slash palette is open it captures the
    // arrows + Tab/Enter for completion; otherwise Tab is swallowed (reserved).
    return CatchEvent(layout, [&state, &screen](Event e) {
        if (e.input() == std::string(1, '\x03')) {
            screen.ExitLoopClosure()();
            return true;
        }
        if (state.tab_index == 0) {
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
