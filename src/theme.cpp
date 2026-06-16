#include "theme.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <utility>

namespace fs = std::filesystem;

// --- Built-in themes --------------------------------------------------------
// These are the defaults seeded to disk on first run; after that the JSON files
// in theme_dir() are the source of truth (edit them, or drop in your own).

// Dark surfaces, but semantic colors are kept so status / view / tokens read at
// a glance.
static Theme dark() {
    Theme t;
    t.name = "dark";
    t.bg = {22, 22, 24};
    t.bg_focus = {28, 28, 32};
    t.panel = {34, 34, 38};
    t.panel_focus = {46, 46, 52};
    t.panel_alt = {50, 50, 56};
    t.text = {216, 216, 220};
    t.text_dim = {120, 120, 128};
    t.accent = {240, 170, 90};    // warm amber: logo, prompt marker
    t.select = {120, 195, 230};   // cyan: active view
    t.model = {240, 170, 90};     // amber: model name
    t.token_in = {120, 195, 230}; // cyan: in tokens
    t.token_out = {150, 210, 130};// green: out tokens
    t.user_bg = {48, 48, 56};
    t.user_fg = {245, 245, 250};
    t.code_bg = {40, 40, 46};     // code block background
    t.code_fg = {180, 210, 160};  // soft green code text
    t.ok = {130, 200, 120};       // green: ready
    t.warn = {235, 190, 90};      // amber: streaming
    t.err = {235, 120, 110};      // red: error
    return t;
}

// Like dark, but the surfaces are pushed roughly midway toward black for a
// dimmer, higher-contrast feel; the accent hues are kept.
static Theme midnight() {
    Theme t;
    t.name = "midnight";
    t.bg = {12, 12, 14};
    t.bg_focus = {18, 18, 21};
    t.panel = {24, 24, 28};
    t.panel_focus = {34, 34, 40};
    t.panel_alt = {38, 38, 44};
    t.text = {210, 210, 216};
    t.text_dim = {108, 108, 118};
    t.accent = {240, 170, 90};
    t.select = {120, 195, 230};
    t.model = {240, 170, 90};
    t.token_in = {120, 195, 230};
    t.token_out = {150, 210, 130};
    t.user_bg = {34, 34, 42};
    t.user_fg = {244, 244, 250};
    t.code_bg = {28, 28, 34};
    t.code_fg = {180, 210, 160};
    t.ok = {130, 200, 120};
    t.warn = {235, 190, 90};
    t.err = {235, 120, 110};
    return t;
}

// Light theme: the surface order inverts (bg is the lightest, input/selection
// the darkest) so the same "focused = a touch brighter" rule still reads.
static Theme light() {
    Theme t;
    t.name = "light";
    t.bg = {238, 238, 233};
    t.bg_focus = {250, 250, 247};
    t.panel = {228, 228, 221};
    t.panel_focus = {238, 238, 232};
    t.panel_alt = {214, 214, 205};
    t.text = {40, 40, 44};
    t.text_dim = {122, 122, 126};
    t.accent = {196, 108, 18};    // burnt orange
    t.select = {38, 128, 188};    // blue
    t.model = {196, 108, 18};
    t.token_in = {38, 128, 188};
    t.token_out = {58, 148, 64};
    t.user_bg = {220, 229, 240};
    t.user_fg = {20, 20, 24};
    t.code_bg = {226, 226, 217};
    t.code_fg = {78, 110, 48};
    t.ok = {48, 148, 60};
    t.warn = {196, 138, 18};
    t.err = {198, 58, 48};
    return t;
}

// Solarized dark: Ethan Schoonover's low-contrast blue-greys with the signature
// yellow / blue / cyan accents.
static Theme solarized_dark() {
    Theme t;
    t.name = "solarized-dark";
    t.bg = {0, 43, 54};            // base03
    t.bg_focus = {2, 50, 62};
    t.panel = {7, 54, 66};         // base02
    t.panel_focus = {13, 64, 77};
    t.panel_alt = {20, 72, 86};
    t.text = {131, 148, 150};      // base0
    t.text_dim = {88, 110, 117};   // base01
    t.accent = {181, 137, 0};      // yellow
    t.select = {38, 139, 210};     // blue
    t.model = {203, 75, 22};       // orange
    t.token_in = {38, 139, 210};
    t.token_out = {133, 153, 0};   // green
    t.user_bg = {7, 54, 66};
    t.user_fg = {147, 161, 161};   // base1
    t.code_bg = {7, 54, 66};
    t.code_fg = {42, 161, 152};    // cyan
    t.ok = {133, 153, 0};
    t.warn = {181, 137, 0};
    t.err = {220, 50, 47};         // red
    return t;
}

// Solarized light: the same accents over the warm parchment surfaces.
static Theme solarized_light() {
    Theme t;
    t.name = "solarized-light";
    t.bg = {253, 246, 227};        // base3
    t.bg_focus = {255, 250, 236};
    t.panel = {238, 232, 213};     // base2
    t.panel_focus = {245, 240, 223};
    t.panel_alt = {228, 221, 199};
    t.text = {101, 123, 131};      // base00
    t.text_dim = {147, 161, 161};  // base1
    t.accent = {181, 137, 0};      // yellow
    t.select = {38, 139, 210};     // blue
    t.model = {203, 75, 22};       // orange
    t.token_in = {38, 139, 210};
    t.token_out = {133, 153, 0};   // green
    t.user_bg = {238, 232, 213};
    t.user_fg = {88, 110, 117};    // base01
    t.code_bg = {238, 232, 213};
    t.code_fg = {42, 161, 152};    // cyan
    t.ok = {133, 153, 0};
    t.warn = {181, 137, 0};
    t.err = {220, 50, 47};         // red
    return t;
}

// Nord: cool blue-grey polar-night surfaces with frost/aurora accents.
static Theme nord() {
    Theme t;
    t.name = "nord";
    t.bg = {46, 52, 64};          // nord0
    t.bg_focus = {52, 59, 73};
    t.panel = {59, 66, 82};        // nord1
    t.panel_focus = {67, 76, 94};  // nord2
    t.panel_alt = {76, 86, 106};   // nord3
    t.text = {216, 222, 233};      // nord4
    t.text_dim = {126, 134, 152};
    t.accent = {136, 192, 208};    // frost cyan (nord8)
    t.select = {129, 161, 193};    // frost blue (nord9)
    t.model = {143, 188, 187};     // frost teal (nord7)
    t.token_in = {129, 161, 193};
    t.token_out = {163, 190, 140}; // aurora green (nord14)
    t.user_bg = {59, 66, 82};
    t.user_fg = {236, 239, 244};   // nord6
    t.code_bg = {59, 66, 82};
    t.code_fg = {163, 190, 140};
    t.ok = {163, 190, 140};        // green
    t.warn = {235, 203, 139};      // aurora yellow (nord13)
    t.err = {191, 97, 106};        // aurora red (nord11)
    return t;
}

// Gruvbox: warm retro browns with bright orange / yellow / green accents.
static Theme gruvbox() {
    Theme t;
    t.name = "gruvbox";
    t.bg = {40, 40, 40};           // bg0
    t.bg_focus = {50, 48, 44};
    t.panel = {60, 56, 54};        // bg1
    t.panel_focus = {80, 73, 69};  // bg2
    t.panel_alt = {102, 92, 84};   // bg3
    t.text = {235, 219, 178};      // fg1
    t.text_dim = {146, 131, 116};  // gray
    t.accent = {254, 128, 25};     // orange
    t.select = {131, 165, 152};    // blue
    t.model = {250, 189, 47};      // yellow
    t.token_in = {131, 165, 152};
    t.token_out = {184, 187, 38};  // green
    t.user_bg = {60, 56, 54};
    t.user_fg = {251, 241, 199};   // fg0
    t.code_bg = {60, 56, 54};
    t.code_fg = {142, 192, 124};   // aqua
    t.ok = {184, 187, 38};         // green
    t.warn = {250, 189, 47};       // yellow
    t.err = {251, 73, 52};         // red
    return t;
}

// Dracula: dark slate-purple surfaces with vivid pink / cyan / green accents.
static Theme dracula() {
    Theme t;
    t.name = "dracula";
    t.bg = {40, 42, 54};
    t.bg_focus = {48, 50, 64};
    t.panel = {50, 52, 66};
    t.panel_focus = {60, 63, 82};
    t.panel_alt = {68, 71, 90};    // current line
    t.text = {248, 248, 242};
    t.text_dim = {98, 114, 164};   // comment
    t.accent = {189, 147, 249};    // purple
    t.select = {139, 233, 253};    // cyan
    t.model = {255, 121, 198};     // pink
    t.token_in = {139, 233, 253};
    t.token_out = {80, 250, 123};  // green
    t.user_bg = {68, 71, 90};
    t.user_fg = {248, 248, 242};
    t.code_bg = {50, 52, 66};
    t.code_fg = {80, 250, 123};
    t.ok = {80, 250, 123};         // green
    t.warn = {255, 184, 108};      // orange
    t.err = {255, 85, 85};         // red
    return t;
}

// Monokai: the classic dark olive ground with hot pink / cyan / lime accents.
static Theme monokai() {
    Theme t;
    t.name = "monokai";
    t.bg = {39, 40, 34};
    t.bg_focus = {46, 47, 40};
    t.panel = {52, 53, 45};
    t.panel_focus = {62, 63, 54};
    t.panel_alt = {73, 72, 62};
    t.text = {248, 248, 242};
    t.text_dim = {117, 113, 94};   // comment
    t.accent = {249, 38, 114};     // pink
    t.select = {102, 217, 239};    // cyan
    t.model = {253, 151, 31};      // orange
    t.token_in = {102, 217, 239};
    t.token_out = {166, 226, 46};  // green
    t.user_bg = {52, 53, 45};
    t.user_fg = {248, 248, 242};
    t.code_bg = {52, 53, 45};
    t.code_fg = {230, 219, 116};   // yellow
    t.ok = {166, 226, 46};
    t.warn = {253, 151, 31};
    t.err = {249, 38, 114};
    return t;
}

// One Dark: Atom's muted blue-grey with soft red / green / blue accents.
static Theme one_dark() {
    Theme t;
    t.name = "one-dark";
    t.bg = {40, 44, 52};
    t.bg_focus = {47, 52, 62};
    t.panel = {48, 53, 63};
    t.panel_focus = {60, 66, 78};
    t.panel_alt = {62, 68, 81};
    t.text = {171, 178, 191};
    t.text_dim = {92, 99, 112};
    t.accent = {97, 175, 239};     // blue
    t.select = {198, 120, 221};    // purple
    t.model = {209, 154, 102};     // orange
    t.token_in = {86, 182, 194};   // cyan
    t.token_out = {152, 195, 121}; // green
    t.user_bg = {48, 53, 63};
    t.user_fg = {220, 223, 228};
    t.code_bg = {33, 37, 43};
    t.code_fg = {152, 195, 121};
    t.ok = {152, 195, 121};
    t.warn = {229, 192, 123};      // yellow
    t.err = {224, 108, 117};       // red
    return t;
}

// Tokyo Night: deep indigo surfaces with bright blue / magenta / green accents.
static Theme tokyo_night() {
    Theme t;
    t.name = "tokyo-night";
    t.bg = {26, 27, 38};
    t.bg_focus = {32, 33, 46};
    t.panel = {36, 40, 59};
    t.panel_focus = {46, 51, 73};
    t.panel_alt = {52, 58, 82};
    t.text = {192, 202, 245};
    t.text_dim = {86, 95, 137};
    t.accent = {122, 162, 247};    // blue
    t.select = {125, 207, 255};    // cyan
    t.model = {187, 154, 247};     // magenta
    t.token_in = {125, 207, 255};
    t.token_out = {158, 206, 106}; // green
    t.user_bg = {36, 40, 59};
    t.user_fg = {205, 214, 250};
    t.code_bg = {36, 40, 59};
    t.code_fg = {158, 206, 106};
    t.ok = {158, 206, 106};
    t.warn = {224, 175, 104};      // yellow
    t.err = {247, 118, 142};       // red
    return t;
}

// Catppuccin Mocha: cozy lavender-grey with mauve / blue / pink accents.
static Theme catppuccin() {
    Theme t;
    t.name = "catppuccin";
    t.bg = {30, 30, 46};           // base
    t.bg_focus = {36, 36, 54};
    t.panel = {49, 50, 68};        // surface0
    t.panel_focus = {69, 71, 90};  // surface1
    t.panel_alt = {88, 91, 112};   // surface2
    t.text = {205, 214, 244};
    t.text_dim = {108, 112, 134};  // overlay0
    t.accent = {203, 166, 247};    // mauve
    t.select = {137, 180, 250};    // blue
    t.model = {245, 194, 231};     // pink
    t.token_in = {137, 220, 235};  // sky
    t.token_out = {166, 227, 161}; // green
    t.user_bg = {49, 50, 68};
    t.user_fg = {205, 214, 244};
    t.code_bg = {49, 50, 68};
    t.code_fg = {148, 226, 213};   // teal
    t.ok = {166, 227, 161};
    t.warn = {249, 226, 175};      // yellow
    t.err = {243, 139, 168};       // red
    return t;
}

// Rose Pine: muted rose / iris / foam over a soft plum night.
static Theme rose_pine() {
    Theme t;
    t.name = "rose-pine";
    t.bg = {25, 23, 36};           // base
    t.bg_focus = {31, 29, 46};     // surface
    t.panel = {38, 35, 58};        // overlay
    t.panel_focus = {50, 47, 73};
    t.panel_alt = {64, 61, 82};    // highlight
    t.text = {224, 222, 244};
    t.text_dim = {110, 106, 134};  // muted
    t.accent = {196, 167, 231};    // iris
    t.select = {156, 207, 216};    // foam
    t.model = {235, 188, 186};     // rose
    t.token_in = {49, 116, 143};   // pine
    t.token_out = {156, 207, 216}; // foam
    t.user_bg = {38, 35, 58};
    t.user_fg = {224, 222, 244};
    t.code_bg = {38, 35, 58};
    t.code_fg = {246, 193, 119};   // gold
    t.ok = {156, 207, 216};        // foam
    t.warn = {246, 193, 119};      // gold
    t.err = {235, 111, 146};       // love
    return t;
}

// Everforest: a calm, low-saturation forest green palette.
static Theme everforest() {
    Theme t;
    t.name = "everforest";
    t.bg = {45, 53, 59};
    t.bg_focus = {52, 63, 68};
    t.panel = {61, 72, 77};
    t.panel_focus = {71, 82, 88};
    t.panel_alt = {86, 97, 102};
    t.text = {211, 198, 170};
    t.text_dim = {133, 146, 137};
    t.accent = {167, 192, 128};    // green
    t.select = {127, 187, 179};    // blue
    t.model = {219, 188, 127};     // yellow
    t.token_in = {127, 187, 179};
    t.token_out = {131, 192, 146}; // aqua
    t.user_bg = {61, 72, 77};
    t.user_fg = {230, 220, 195};
    t.code_bg = {61, 72, 77};
    t.code_fg = {131, 192, 146};
    t.ok = {167, 192, 128};
    t.warn = {219, 188, 127};
    t.err = {230, 126, 128};       // red
    return t;
}

// Palenight: Material's soft indigo with purple / cyan / green accents.
static Theme palenight() {
    Theme t;
    t.name = "palenight";
    t.bg = {41, 45, 62};
    t.bg_focus = {48, 52, 71};
    t.panel = {52, 57, 77};
    t.panel_focus = {63, 68, 92};
    t.panel_alt = {68, 74, 99};
    t.text = {166, 172, 205};
    t.text_dim = {103, 110, 149};
    t.accent = {199, 146, 234};    // purple
    t.select = {130, 170, 255};    // blue
    t.model = {247, 140, 108};     // orange
    t.token_in = {137, 221, 255};  // cyan
    t.token_out = {195, 232, 141}; // green
    t.user_bg = {52, 57, 77};
    t.user_fg = {186, 192, 222};
    t.code_bg = {52, 57, 77};
    t.code_fg = {195, 232, 141};
    t.ok = {195, 232, 141};
    t.warn = {255, 203, 107};      // yellow
    t.err = {240, 113, 120};       // red
    return t;
}

// Ayu Mirage: warm slate with amber / blue / lime accents.
static Theme ayu_mirage() {
    Theme t;
    t.name = "ayu-mirage";
    t.bg = {31, 36, 48};
    t.bg_focus = {37, 42, 55};
    t.panel = {35, 40, 52};
    t.panel_focus = {48, 54, 68};
    t.panel_alt = {58, 64, 79};
    t.text = {204, 202, 194};
    t.text_dim = {92, 103, 115};
    t.accent = {255, 204, 102};    // amber
    t.select = {115, 208, 255};    // blue
    t.model = {255, 167, 89};      // orange
    t.token_in = {92, 207, 230};   // cyan
    t.token_out = {213, 255, 128}; // green
    t.user_bg = {35, 40, 52};
    t.user_fg = {225, 223, 215};
    t.code_bg = {35, 40, 52};
    t.code_fg = {213, 255, 128};
    t.ok = {186, 230, 126};
    t.warn = {255, 204, 102};
    t.err = {242, 135, 121};       // red
    return t;
}

// Night Owl: an inky navy ground tuned for low light.
static Theme night_owl() {
    Theme t;
    t.name = "night-owl";
    t.bg = {1, 22, 39};
    t.bg_focus = {5, 29, 48};
    t.panel = {8, 38, 60};
    t.panel_focus = {15, 52, 78};
    t.panel_alt = {22, 62, 90};
    t.text = {214, 222, 235};
    t.text_dim = {99, 119, 119};
    t.accent = {130, 170, 255};    // blue
    t.select = {127, 219, 202};    // cyan
    t.model = {199, 146, 234};     // purple
    t.token_in = {127, 219, 202};
    t.token_out = {173, 219, 103}; // green
    t.user_bg = {8, 38, 60};
    t.user_fg = {214, 222, 235};
    t.code_bg = {8, 38, 60};
    t.code_fg = {173, 219, 103};
    t.ok = {173, 219, 103};
    t.warn = {236, 196, 141};      // yellow
    t.err = {239, 83, 80};         // red
    return t;
}

// Cobalt2: a saturated marine blue with a bright yellow accent.
static Theme cobalt() {
    Theme t;
    t.name = "cobalt";
    t.bg = {15, 32, 45};
    t.bg_focus = {20, 41, 57};
    t.panel = {25, 53, 73};
    t.panel_focus = {33, 66, 90};
    t.panel_alt = {40, 78, 105};
    t.text = {232, 240, 247};
    t.text_dim = {110, 140, 160};
    t.accent = {255, 198, 0};      // yellow
    t.select = {0, 136, 255};      // blue
    t.model = {255, 157, 0};       // orange
    t.token_in = {128, 252, 255};  // cyan
    t.token_out = {58, 217, 0};    // green
    t.user_bg = {25, 53, 73};
    t.user_fg = {255, 255, 255};
    t.code_bg = {25, 53, 73};
    t.code_fg = {128, 252, 255};
    t.ok = {58, 217, 0};
    t.warn = {255, 198, 0};
    t.err = {255, 98, 140};        // pink
    return t;
}

// Zenburn: a low-contrast, warm grey-green palette that's easy on the eyes.
static Theme zenburn() {
    Theme t;
    t.name = "zenburn";
    t.bg = {63, 63, 63};
    t.bg_focus = {72, 72, 70};
    t.panel = {74, 74, 72};
    t.panel_focus = {86, 86, 82};
    t.panel_alt = {95, 95, 90};
    t.text = {220, 220, 204};
    t.text_dim = {127, 159, 127};  // greenish comment
    t.accent = {240, 223, 175};    // yellow
    t.select = {140, 208, 211};    // blue
    t.model = {223, 175, 143};     // orange
    t.token_in = {147, 224, 227};  // cyan
    t.token_out = {159, 197, 159}; // green
    t.user_bg = {74, 74, 72};
    t.user_fg = {240, 240, 224};
    t.code_bg = {74, 74, 72};
    t.code_fg = {159, 197, 159};
    t.ok = {159, 197, 159};
    t.warn = {240, 223, 175};
    t.err = {204, 147, 147};       // red
    return t;
}

// Kanagawa: Katsushika Hokusai's wave - ink-blue ground, foam and wave accents.
static Theme kanagawa() {
    Theme t;
    t.name = "kanagawa";
    t.bg = {31, 31, 40};
    t.bg_focus = {38, 38, 48};
    t.panel = {42, 42, 55};
    t.panel_focus = {54, 54, 70};
    t.panel_alt = {60, 60, 77};
    t.text = {220, 215, 186};
    t.text_dim = {114, 113, 105};
    t.accent = {126, 156, 216};    // crystal blue
    t.select = {127, 180, 202};    // wave cyan
    t.model = {149, 127, 184};     // violet
    t.token_in = {122, 168, 159};  // teal
    t.token_out = {152, 187, 108}; // green
    t.user_bg = {42, 42, 55};
    t.user_fg = {220, 215, 186};
    t.code_bg = {42, 42, 55};
    t.code_fg = {152, 187, 108};
    t.ok = {152, 187, 108};
    t.warn = {255, 160, 102};      // orange
    t.err = {232, 36, 36};         // red
    return t;
}

static std::vector<Theme> builtin_themes() {
    return {
        dark(), midnight(), light(), solarized_dark(), solarized_light(),
        nord(), gruvbox(), dracula(), monokai(), one_dark(),
        tokyo_night(), catppuccin(), rose_pine(), everforest(), palenight(),
        ayu_mirage(), night_owl(), cobalt(), zenburn(), kanagawa(),
    };
}

// --- Serialization & file IO ------------------------------------------------

namespace {

// The fixed, ordered set of color slots a theme is built from. These are the
// JSON keys under "colors" and the canonical palette a custom theme defines.
struct Slot {
    const char* key;
    Rgb Theme::* field;
};
const std::vector<Slot>& slots() {
    static const std::vector<Slot> s = {
        {"bg", &Theme::bg},                 {"bg_focus", &Theme::bg_focus},
        {"panel", &Theme::panel},           {"panel_focus", &Theme::panel_focus},
        {"panel_alt", &Theme::panel_alt},   {"text", &Theme::text},
        {"text_dim", &Theme::text_dim},     {"accent", &Theme::accent},
        {"select", &Theme::select},         {"model", &Theme::model},
        {"token_in", &Theme::token_in},     {"token_out", &Theme::token_out},
        {"user_bg", &Theme::user_bg},       {"user_fg", &Theme::user_fg},
        {"code_bg", &Theme::code_bg},       {"code_fg", &Theme::code_fg},
        {"ok", &Theme::ok},                 {"warn", &Theme::warn},
        {"err", &Theme::err},
    };
    return s;
}

std::string to_hex(const Rgb& c) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r & 0xFF, c.g & 0xFF, c.b & 0xFF);
    return buf;
}

bool parse_hex(const std::string& in, Rgb& out) {
    std::string h = (!in.empty() && in[0] == '#') ? in.substr(1) : in;
    if (h.size() != 6) {
        return false;
    }
    try {
        out.r = std::stoi(h.substr(0, 2), nullptr, 16);
        out.g = std::stoi(h.substr(2, 2), nullptr, 16);
        out.b = std::stoi(h.substr(4, 2), nullptr, 16);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

nlohmann::json to_json(const Theme& t) {
    nlohmann::json colors = nlohmann::json::object();
    for (const auto& s : slots()) {
        colors[s.key] = to_hex(t.*(s.field));
    }
    return nlohmann::json{{"name", t.name}, {"colors", colors}};
}

// Overlay the colors present in `j` onto `out` (which carries the defaults/base),
// so a partial file still yields a usable theme. Returns false if there's no name.
bool from_json(const nlohmann::json& j, Theme& out) {
    out.name = j.value("name", out.name);
    if (out.name.empty()) {
        return false;
    }
    const nlohmann::json& colors = j.contains("colors") ? j["colors"] : j;
    for (const auto& s : slots()) {
        if (colors.contains(s.key) && colors[s.key].is_string()) {
            Rgb c;
            if (parse_hex(colors[s.key].get<std::string>(), c)) {
                out.*(s.field) = c;
            }
        }
    }
    return true;
}

fs::path config_dir() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        return fs::path(xdg) / "hearth";
    }
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : ".") / ".config" / "hearth";
}

fs::path themes_path() { return config_dir() / "themes"; }

std::string slugify(const std::string& s) {
    std::string out;
    for (char ch : s) {
        if (std::isalnum((unsigned char)ch) || ch == '-' || ch == '_') {
            out += (char)std::tolower((unsigned char)ch);
        } else if (ch == ' ') {
            out += '-';
        }
    }
    return out.empty() ? std::string("theme") : out.substr(0, 60);
}

fs::path theme_file(const std::string& name) { return themes_path() / (slugify(name) + ".json"); }

bool write_theme_file(const Theme& t) {
    try {
        std::error_code ec;
        fs::create_directories(themes_path(), ec);
        std::ofstream out(theme_file(t.name));
        if (!out) {
            return false;
        }
        out << to_json(t).dump(2);
        return out.good();
    } catch (const std::exception&) {
        return false;
    }
}

bool read_theme_file(const fs::path& p, Theme& out) {
    try {
        std::ifstream in(p);
        if (!in) {
            return false;
        }
        nlohmann::json j;
        in >> j;
        return from_json(j, out);
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<Theme>& registry() {
    static std::vector<Theme> r;
    return r;
}

}  // namespace

void load_themes() {
    auto& reg = registry();
    reg.clear();
    const std::vector<Theme> builtins = builtin_themes();

    std::error_code ec;
    fs::create_directories(themes_path(), ec);
    for (const auto& t : builtins) {  // seed any built-in missing from disk
        if (!fs::exists(theme_file(t.name), ec)) {
            write_theme_file(t);
        }
    }

    // Built-ins first, in their defined order, preferring the on-disk copy so a
    // user's edits to a built-in file take effect.
    std::set<std::string> seen;
    for (const auto& t : builtins) {
        Theme loaded = t;
        read_theme_file(theme_file(t.name), loaded);
        loaded.name = t.name;  // a built-in keeps its canonical name
        reg.push_back(std::move(loaded));
        seen.insert(t.name);
    }

    // Then any custom themes (files that aren't a built-in), sorted by name.
    std::vector<Theme> custom;
    if (fs::exists(themes_path(), ec)) {
        for (const auto& entry : fs::directory_iterator(themes_path(), ec)) {
            if (entry.path().extension() != ".json") {
                continue;
            }
            Theme t;
            if (read_theme_file(entry.path(), t) && !seen.count(t.name)) {
                custom.push_back(std::move(t));
                seen.insert(custom.back().name);
            }
        }
    }
    std::sort(custom.begin(), custom.end(),
              [](const Theme& a, const Theme& b) { return a.name < b.name; });
    for (auto& t : custom) {
        reg.push_back(std::move(t));
    }

    if (reg.empty()) {  // filesystem unavailable: fall back to the in-memory built-ins
        reg = builtins;
    }
}

bool save_theme(const Theme& t) {
    const bool ok = write_theme_file(t);
    load_themes();  // refresh the registry so the change is visible immediately
    return ok;
}

bool delete_theme(const std::string& name) {
    if (is_builtin(name)) {
        return false;
    }
    std::error_code ec;
    fs::remove(theme_file(name), ec);
    load_themes();
    return !ec;
}

bool is_builtin(const std::string& name) {
    static const std::set<std::string> names = [] {
        std::set<std::string> s;
        for (const auto& t : builtin_themes()) {
            s.insert(t.name);
        }
        return s;
    }();
    return names.count(name) > 0;
}

const std::vector<ThemeSlot>& theme_palette() {
    static const std::vector<ThemeSlot> p = {
        {"Background", &Theme::bg},      {"Surface", &Theme::panel},
        {"Highlight", &Theme::panel_alt},{"Text", &Theme::text},
        {"Subtext", &Theme::text_dim},   {"Accent", &Theme::accent},
        {"Select", &Theme::select},      {"Success", &Theme::ok},
        {"Warning", &Theme::warn},       {"Error", &Theme::err},
        {"Code", &Theme::code_fg},
    };
    return p;
}

void finalize_theme(Theme& t) {
    auto lighten = [](Rgb c, int d) {
        auto cl = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
        return Rgb{cl(c.r + d), cl(c.g + d), cl(c.b + d)};
    };
    t.bg_focus = lighten(t.bg, 6);        // focused panels read a touch brighter
    t.panel_focus = lighten(t.panel, 12);
    t.model = t.accent;                   // the model name shares the accent
    t.token_in = t.select;
    t.token_out = t.ok;                   // both greens
    t.user_bg = t.panel_alt;
    t.user_fg = t.text;
    t.code_bg = t.panel;
}

std::string theme_dir() { return themes_path().string(); }

const std::vector<Theme>& themes() {
    if (registry().empty()) {
        load_themes();
    }
    return registry();
}

const Theme& theme_by_name(const std::string& name) {
    for (const auto& t : themes()) {
        if (t.name == name) {
            return t;
        }
    }
    return themes().front();
}
