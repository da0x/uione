// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Docs pages are written in markdown and turned into HTML when the site is built,
// so the app needs no markdown code of its own. md4c does the conversion, with
// GitHub's flavor of markdown, which includes tables.

#pragma once

#include <set>
#include <string>
#include <string_view>

extern "C" {
#include <md4c-html.h>
}

namespace one::generators {

    // Every heading gets an id from its words, as GitHub gives them, so a section can
    // be linked to: "Built-in types" is #built-in-types. Markup in a heading, like
    // `code`, is left out, and a heading whose id is taken gets -1, -2 and so on.
    inline std::string anchor_headings(const std::string& html) {
        std::string out;
        std::set<std::string> taken;
        std::size_t at = 0;
        while (at < html.size()) {
            std::size_t open = html.find("<h", at);
            if (open == std::string::npos || open + 3 >= html.size() || html[open + 2] < '1' || html[open + 2] > '6' || html[open + 3] != '>') {
                if (open == std::string::npos) break;
                out.append(html, at, open + 2 - at);
                at = open + 2;
                continue;
            }
            char level = html[open + 2];
            std::string close = std::string("</h") + level + ">";
            std::size_t end = html.find(close, open);
            if (end == std::string::npos) break;
            std::string words;
            bool in_tag = false;
            for (std::size_t i = open + 4; i < end; ++i) {
                char c = html[i];
                if (c == '<') in_tag = true;
                else if (c == '>') in_tag = false;
                else if (!in_tag) words += c;
            }
            std::string id;
            for (char c : words) {
                if (c >= 'A' && c <= 'Z') id += static_cast<char>(c - 'A' + 'a');
                else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_') id += c;
                else if (c == ' ') id += '-';
            }
            std::string unique = id;
            for (int n = 1; taken.contains(unique); ++n) unique = id + "-" + std::to_string(n);
            taken.insert(unique);
            out.append(html, at, open - at);
            out += std::string("<h") + level + " id=\"" + unique + "\">";
            at = open + 4;
        }
        out.append(html, at, std::string::npos);
        return out;
    }

    inline std::string markdown_to_html(std::string_view markdown) {
        std::string html;
        md_html(
            markdown.data(), static_cast<MD_SIZE>(markdown.size()),
            [](const MD_CHAR* text, MD_SIZE size, void* into) {
                static_cast<std::string*>(into)->append(text, size);
            },
            &html, MD_DIALECT_GITHUB, 0);
        return anchor_headings(html);
    }

    // A page's title: its first top-level heading, or its slug when it has none.
    inline std::string markdown_title(std::string_view markdown, std::string_view fallback) {
        std::size_t at = 0;
        while (at < markdown.size()) {
            std::size_t end = markdown.find('\n', at);
            std::string_view line = markdown.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
            if (line.starts_with("# ")) return std::string(line.substr(2));
            if (end == std::string_view::npos) break;
            at = end + 1;
        }
        return std::string(fallback);
    }

    // A page's address from its file name, without the number that orders it:
    // 02-language.md becomes language.
    inline std::string markdown_slug(std::string_view stem) {
        std::size_t digits = 0;
        while (digits < stem.size() && stem[digits] >= '0' && stem[digits] <= '9') ++digits;
        if (digits > 0 && digits < stem.size() && stem[digits] == '-') return std::string(stem.substr(digits + 1));
        return std::string(stem);
    }

} // namespace one::generators
