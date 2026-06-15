#pragma once

#include <ftxui/screen/color.hpp>

#include <string>
#include <vector>

// A palette the whole UI draws from. Surfaces are distinguished by background
// shade (bg < panel < panel_alt); the accent/semantic colors carry meaning
// (status, the active view, token counts) and stay colored even in a dark theme.
struct Theme {
    std::string name;

    // Surfaces.
    ftxui::Color bg;          // app background (darkest)
    ftxui::Color bg_focus;    // chat area when the view is focused
    ftxui::Color panel;       // header / sidebar / status bar
    ftxui::Color panel_focus; // sidebar when it is focused
    ftxui::Color panel_alt;   // input box, selected sidebar item

    // Text.
    ftxui::Color text;       // primary text
    ftxui::Color text_dim;   // secondary text

    // Semantic accents.
    ftxui::Color accent;     // logo, prompt marker
    ftxui::Color select;     // active view in the sidebar
    ftxui::Color model;      // model name
    ftxui::Color token_in;   // "in" token count
    ftxui::Color token_out;  // "out" token count
    ftxui::Color user_bg;    // highlighted user message background
    ftxui::Color user_fg;    // user message text
    ftxui::Color code_bg;    // code block / inline code background
    ftxui::Color code_fg;    // code text
    ftxui::Color ok;         // ready
    ftxui::Color warn;       // streaming
    ftxui::Color err;        // errors
};

// All built-in themes, and a lookup that falls back to the first one.
const std::vector<Theme>& themes();
const Theme& theme_by_name(const std::string& name);
