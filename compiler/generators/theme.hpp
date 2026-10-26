// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

// A theme as the page draws it: the CSS variables the component set reads, light and
// dark, from the theme's colors, fonts, corners and depth, with everything else
// worked out from them: text on the accent, hover, the soft accent behind a chosen
// row, the edge of a field, grid lines, the tints of how urgent something is, and
// shadows. Nothing worked out is ever written in a theme, so a theme stays its
// roles and can't drift.

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "language/ast.hpp"

namespace one::generators::theme {

    struct look {
        std::map<std::string, std::pair<std::string, std::string>> colors;  // role: light, dark
        std::string text = "IBM Plex Sans", heading, code = "IBM Plex Mono";
        // What pages set as documents are in, like a license: Caslon, the face much of
        // the eighteenth century's official printing was set in.
        std::string document = "Libre Caslon Text";
        std::string ground, ground_dark;  // the code editor's, when the theme says
        int corners = 10;
        std::string depth = "raised";
        std::string base;  // the built-in theme it starts from, whose shades its sections take
        std::map<std::string, std::string> font_files;  // fonts the app brings, by family: the file's path
    };

    // The theme of that name, with what it's from, from the project's files or uione's
    // own library; nothing when there's none.
    std::optional<look> resolve(const std::vector<language::file>& files, const std::string& name);

    // The variables, light; dark as the system is unless the reader picked; and dark
    // when they did.
    std::string css(const look& l);

    // The same, for a part of a page rather than the page, like a theme designer's
    // preview: under scope, dark with data-theme="dark" on it, and more contrast with
    // data-contrast="more", so light and dark, and more contrast, can be shown side by
    // side whatever the page itself is.
    std::string scoped_css(const look& l, const std::string& scope);

    // The fonts the component set doesn't bring, from Google Fonts; empty when it brings them all.
    // A document's font only when a page is set as one, in its roman, italic and bold.
    std::string fonts_link(const look& l, bool documents = false);

} // namespace one::generators::theme
