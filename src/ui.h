#pragma once

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "app_state.h"

// Builds the full component tree (tabs, chat view, settings view, key handling)
// wired to the given state and screen.
ftxui::Component build_app(AppState& state, ftxui::ScreenInteractive& screen);
