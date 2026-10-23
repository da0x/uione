// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A project's themes and uione's own, for a theme designer.

#pragma once

#include <string>
#include <vector>

#include "driver/check.hpp"
#include "generators/theme.hpp"

namespace one::driver {

    // A project's themes, and uione's own, each as it says and as it's drawn, for a
    // theme designer: where it's declared, what it's from, its colors light and dark,
    // fonts, corners and depth with what it's from filled in, and its CSS for a
    // preview under scope. theme is the one the project's block names, harbor unless
    // it names one; nothing when the files don't parse.
    struct theme_shown {
        std::string name, title, from, path;
        int line = 0;
        bool own = false;  // uione's, from its library, which a project starts from but doesn't change
        generators::theme::look look;
        std::string css;
    };

    struct themes_shown {
        bool read = false;
        std::string theme = "harbor";
        std::string project_path;  // the file with the project block
        std::vector<theme_shown> themes;
    };

    themes_shown themes(const sources& given, const std::string& scope);

    themes_shown themes(const std::string& root, const std::string& scope);

} // namespace one::driver
