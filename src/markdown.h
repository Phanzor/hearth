#pragma once

#include <ftxui/dom/elements.hpp>

#include <string>

#include "theme.h"

// Renders a subset of Markdown (fenced code blocks, inline code, bold/italic,
// headers, bullet lists) into an FTXUI element. Unknown markup falls through as
// plain text.
ftxui::Element render_markdown(const Theme& theme, const std::string& text);
