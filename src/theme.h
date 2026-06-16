#pragma once

#include <ftxui/screen/color.hpp>

#include <string>
#include <vector>

// An 8-bit RGB triple - the serializable unit a theme is built from. It converts
// implicitly to ftxui::Color, so the UI keeps using palette entries directly
// (color(t.accent), bgcolor(t.code_bg), ...) while we still own the raw values
// for reading and writing theme files (ftxui::Color doesn't expose its RGB back).
struct Rgb {
    int r = 0;
    int g = 0;
    int b = 0;
    operator ftxui::Color() const { return ftxui::Color::RGB(r, g, b); }
    bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
};

// A palette the whole UI draws from. Surfaces are distinguished by background
// shade (bg < panel < panel_alt); the accent/semantic colors carry meaning
// (status, the active view, token counts). The full set of fields below is the
// fixed list of color "slots" a theme - built-in or custom - is made of.
struct Theme {
    std::string name;

    // Surfaces.
    Rgb bg;          // app background (darkest)
    Rgb bg_focus;    // chat area when the view is focused
    Rgb panel;       // header / sidebar / status bar
    Rgb panel_focus; // sidebar when it is focused
    Rgb panel_alt;   // input box, selected sidebar item

    // Text.
    Rgb text;        // primary text
    Rgb text_dim;    // secondary text

    // Semantic accents.
    Rgb accent;      // logo, prompt marker
    Rgb select;      // active view in the sidebar
    Rgb model;       // model name
    Rgb token_in;    // "in" token count
    Rgb token_out;   // "out" token count
    Rgb user_bg;     // highlighted user message background
    Rgb user_fg;     // user message text
    Rgb code_bg;     // code block / inline code background
    Rgb code_fg;     // code text
    Rgb ok;          // ready
    Rgb warn;        // streaming
    Rgb err;         // errors
};

// Every theme currently loaded (built-ins first, then custom themes by name).
// Lazily loads from disk on first use; see load_themes().
const std::vector<Theme>& themes();

// Look a theme up by name, falling back to the first (default) theme.
const Theme& theme_by_name(const std::string& name);

// Where theme files live: the XDG config dir, ~/.config/hearth/themes.
std::string theme_dir();

// Populate the in-memory registry from theme_dir(), seeding the built-in themes
// as JSON files on first run. Safe to call more than once (it reloads).
void load_themes();

// Write a theme to theme_dir()/<name>.json and refresh the in-memory registry.
// Returns false on a filesystem error.
bool save_theme(const Theme& t);

// Remove a custom theme's file and refresh the registry. Returns false on a
// filesystem error or if `name` is a built-in (built-ins can't be deleted).
bool delete_theme(const std::string& name);

// Is `name` one of the built-in themes? Built-ins are read-only: they can be
// applied but not edited, renamed, or overwritten.
bool is_builtin(const std::string& name);

// The fixed, user-facing palette a custom theme is built from: a labelled color
// slot per editable color. The remaining Theme fields are derived from these by
// finalize_theme, so the editor only has to expose this short list.
struct ThemeSlot {
    const char* label;
    Rgb Theme::* field;
};
const std::vector<ThemeSlot>& theme_palette();

// Fill a theme's derived fields (focus shades, model, token colors, ...) from
// its editable palette slots, so a theme built in the editor renders completely.
void finalize_theme(Theme& t);
