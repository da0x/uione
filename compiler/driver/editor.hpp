// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What an editor asks while a file is written: what a name means, and what can be
// written where the cursor is.

#pragma once

#include <string>
#include <vector>

#include "driver/check.hpp"
#include "language/token.hpp"

namespace one::driver {

    // What the name at a place in a project's file means, for an editor's hover and
    // its going to a definition: said in words, and where it's declared, or the
    // reference's section for one of the language's own. Nothing, when no name is
    // there, or the project doesn't parse.
    struct definition {
        bool found = false;
        std::string says;
        std::string path;     // where it's declared; empty for the language's own
        int line = 0;
        int column = 0;
        std::string section;  // the reference's section, like built-in-values
        int from = 0;         // the column the name starts at, and the one after it
        int to = 0;
    };

    definition define(const sources& given, const std::string& path, int line, int column);

    definition define(const std::string& root, const std::string& path, int line, int column);

    // What can be written where an editor's cursor is, from the library: a project's
    // settings in its block, or an environment's in one of those, and the choices of
    // an enum setting after its name. It reads the text around the cursor, so it works
    // while the file is half written and doesn't parse.
    struct completion_item {
        std::string label;   // what's written, like corners or square
        std::string detail;  // its type, or how a choice is shown
        std::string info;    // what the library says of it
    };

    struct completions {
        int from = 0;  // the column the word being written starts at
        std::vector<completion_item> items;
    };

    completions complete(const std::string& text, int line, int column);

} // namespace one::driver
