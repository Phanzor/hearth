#include <ftxui/component/screen_interactive.hpp>

#include "app_state.h"
#include "config.h"
#include "storage.h"
#include "ui.h"

int main() {
    AppState state;
    state.config = load_config();
    state.theme = theme_by_name(state.config.theme);
    storage::load(state.conversations, state.archived);

    auto screen = ftxui::ScreenInteractive::Fullscreen();
    auto app = build_app(state, screen);
    screen.Loop(app);
    return 0;
}
