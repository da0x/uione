// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Docs pages are written in markdown and turned into HTML when the site is built,
// so the app needs no markdown code of its own. md4c does the conversion, with
// GitHub's flavor of markdown, which includes tables.

#pragma once

#include <string>
#include <string_view>

namespace one::generators {

    // Every heading gets an id from its words, as GitHub gives them, so a section can
    // be linked to: "Built-in types" is #built-in-types. Markup in a heading, like
    // `code`, is left out, and a heading whose id is taken gets -1, -2 and so on.
    std::string anchor_headings(const std::string& html);

    std::string markdown_to_html(std::string_view markdown);

    // A page's title: its first top-level heading, or its slug when it has none.
    std::string markdown_title(std::string_view markdown, std::string_view fallback);

    // A page's address from its file name, without the number that orders it:
    // 02-language.md becomes language.
    std::string markdown_slug(std::string_view stem);

} // namespace one::generators
