#include "theme.h"

using ftxui::Color;

// Dark surfaces, but semantic colors are kept so status / view / tokens read at
// a glance.
static Theme dark() {
    Theme t;
    t.name = "dark";
    t.bg = Color::RGB(22, 22, 24);
    t.panel = Color::RGB(34, 34, 38);
    t.panel_alt = Color::RGB(50, 50, 56);
    t.text = Color::RGB(216, 216, 220);
    t.text_dim = Color::RGB(120, 120, 128);
    t.accent = Color::RGB(240, 170, 90);    // warm amber: logo, prompt marker
    t.select = Color::RGB(120, 195, 230);   // cyan: active view
    t.model = Color::RGB(240, 170, 90);     // amber: model name
    t.token_in = Color::RGB(120, 195, 230); // cyan: in tokens
    t.token_out = Color::RGB(150, 210, 130);// green: out tokens
    t.user_bg = Color::RGB(48, 48, 56);
    t.user_fg = Color::RGB(245, 245, 250);
    t.code_bg = Color::RGB(40, 40, 46);     // code block background
    t.code_fg = Color::RGB(180, 210, 160);  // soft green code text
    t.ok = Color::RGB(130, 200, 120);       // green: ready
    t.warn = Color::RGB(235, 190, 90);      // amber: streaming
    t.err = Color::RGB(235, 120, 110);      // red: error
    return t;
}

const std::vector<Theme>& themes() {
    // Add new themes here; they become available everywhere by name.
    static const std::vector<Theme> all = {dark()};
    return all;
}

const Theme& theme_by_name(const std::string& name) {
    for (const auto& t : themes()) {
        if (t.name == name) {
            return t;
        }
    }
    return themes().front();
}
