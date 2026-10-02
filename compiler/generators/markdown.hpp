// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Docs pages are written in markdown and turned into HTML when the site is built,
// so the app needs no markdown code of its own. md4c does the conversion, with
// GitHub's flavor of markdown, which includes tables.

#pragma once

#include <string>
#include <string_view>

extern "C" {
#include <md4c-html.h>
}

namespace one::generators {

    inline std::string markdown_to_html(std::string_view markdown) {
        std::string html;
        md_html(
            markdown.data(), static_cast<MD_SIZE>(markdown.size()),
            [](const MD_CHAR* text, MD_SIZE size, void* into) {
                static_cast<std::string*>(into)->append(text, size);
            },
            &html, MD_DIALECT_GITHUB, 0);
        return html;
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
