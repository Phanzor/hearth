#include "markdown.h"

#include <ftxui/dom/flexbox_config.hpp>

#include <sstream>
#include <utility>
#include <vector>

using namespace ftxui;

namespace {

enum class Span { plain, bold, code };

std::string ltrim(const std::string& s) {
    const size_t i = s.find_first_not_of(" \t");
    return i == std::string::npos ? "" : s.substr(i);
}

// Split a line into styled runs at `code`, **bold** and *italic* markers.
// Markers that are never closed are treated as plain text.
std::vector<std::pair<std::string, Span>> parse_spans(const std::string& s) {
    std::vector<std::pair<std::string, Span>> runs;
    std::string plain;
    auto flush = [&] {
        if (!plain.empty()) {
            runs.push_back({plain, Span::plain});
            plain.clear();
        }
    };
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        const char c = s[i];
        if (c == '`') {
            const size_t end = s.find('`', i + 1);
            if (end != std::string::npos) {
                flush();
                runs.push_back({s.substr(i + 1, end - i - 1), Span::code});
                i = end + 1;
                continue;
            }
        } else if (c == '*' && i + 1 < n && s[i + 1] == '*') {
            const size_t end = s.find("**", i + 2);
            if (end != std::string::npos) {
                flush();
                runs.push_back({s.substr(i + 2, end - i - 2), Span::bold});
                i = end + 2;
                continue;
            }
        }
        plain += c;
        i++;
    }
    flush();
    return runs;
}

Element styled_word(const Theme& t, const std::string& w, Span kind) {
    switch (kind) {
        case Span::bold:
            return text(w) | bold | color(t.text);
        case Span::code:
            return text(w) | color(t.code_fg) | bgcolor(t.code_bg);
        default:
            return text(w) | color(t.text);
    }
}

// Lay out a line's words in a wrapping flexbox (a styled `paragraph`). Each
// space stays attached to its word rather than being a layout gap, so a mouse
// selection copies the spaces too (FTXUI's selection can't see flexbox gaps).
Element inline_flow(const Theme& t, const std::string& s) {
    Elements words;
    for (const auto& [run, kind] : parse_spans(s)) {
        size_t start = 0;
        while (start < run.size()) {
            const size_t sp = run.find(' ', start);
            const size_t len = (sp == std::string::npos) ? std::string::npos : sp - start + 1;
            words.push_back(styled_word(t, run.substr(start, len), kind));
            if (sp == std::string::npos) {
                break;
            }
            start = sp + 1;
        }
    }
    if (words.empty()) {
        return text("");
    }
    return flexbox(std::move(words));
}

Element render_line(const Theme& t, const std::string& line) {
    if (line.rfind("### ", 0) == 0) {
        return text(line.substr(4)) | bold | color(t.accent);
    }
    if (line.rfind("## ", 0) == 0) {
        return text(line.substr(3)) | bold | color(t.accent);
    }
    if (line.rfind("# ", 0) == 0) {
        return text(line.substr(2)) | bold | color(t.accent);
    }
    const std::string lt = ltrim(line);
    if (lt.rfind("- ", 0) == 0 || lt.rfind("* ", 0) == 0) {
        return hbox({text("  • ") | color(t.accent), inline_flow(t, lt.substr(2))});
    }
    return inline_flow(t, line);
}

}  // namespace

Element render_markdown(const Theme& t, const std::string& src) {
    Elements out;
    std::vector<std::string> code;
    bool in_code = false;

    auto flush_code = [&] {
        Elements rows;
        for (const auto& cl : code) {
            // Full-width background: text + filler share one bgcolor box.
            rows.push_back(hbox({text(" " + cl) | color(t.code_fg), filler()}) | bgcolor(t.code_bg));
        }
        out.push_back(vbox(std::move(rows)));
        code.clear();
    };

    std::stringstream ss(src);
    std::string line;
    while (std::getline(ss, line)) {
        if (ltrim(line).rfind("```", 0) == 0) {
            if (in_code) {
                flush_code();
            }
            in_code = !in_code;
            continue;  // drop the fence line itself
        }
        if (in_code) {
            code.push_back(line);
            continue;
        }
        out.push_back(render_line(t, line));
    }
    if (in_code) {  // unterminated fence
        flush_code();
    }
    if (out.empty()) {
        out.push_back(text(""));
    }
    return vbox(std::move(out));
}
