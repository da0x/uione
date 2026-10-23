// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Where things are in a project's files, for an editor: the project block's parts,
// and each screen's.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "language/ast.hpp"

namespace one::driver {

    // Where a project block's parts are, for an editor that changes them: each
    // setting's line, and each environment's first and last, by the block's file.
    struct outline {
        struct setting {
            std::string key;
            std::string value;
            int line = 0;
        };
        struct environment {
            std::string name;
            int line = 0;
            int end = 0;  // the line of its closing brace
            std::vector<setting> settings;
        };
        std::string path;
        std::string name;
        int line = 0;
        int end = 0;
        std::vector<setting> settings;
        std::vector<environment> environments;
    };

    // A screen's parts, for an editor that lays it out: its layout, and each item
    // where it starts, a region holding its own. An item is named by its kind, like
    // table or button, what it shows or runs, and what it says, when it says
    // something.
    //
    // A table says more: its columns, and how it's divided, searched, sorted and
    // paged, each with its line, or 0 when it isn't; and what its rows hold, which is
    // what it can show, the ones with choices marked, since those are what it can be
    // divided by.
    struct outlined_column {
        std::string value;  // implemented_by.name
        std::string label;  // "Implementing", when it has one
        bool labeled = false;
        int line = 0;
        std::string when;   // person != me, as written, on a row's button
    };

    struct outlined_row {
        std::string name;
        bool choices = false;
    };

    struct outlined_table {
        std::vector<outlined_column> columns;
        std::vector<outlined_row> rows;
        std::string by;
        std::vector<std::string> search;
        std::string sort;  // -number, for the largest first
        int page = 0;
        std::string link;
    };

    struct outlined_item {
        std::string kind;     // table, form, button, text, region, ...
        std::string subject;  // projects::issue_page.comments, issue::close, main
        std::string label;    // "Close issue", when it has one
        int line = 0;
        std::vector<outlined_item> items;  // a region's, a section's or a menu's
        std::optional<outlined_table> table;
    };

    struct outlined_screen {
        std::string path;
        std::string title;
        std::string route;
        int line = 0;
        std::string layout;   // as written; empty when the project's is used
        int layout_line = 0;  // where it's written, when it is
        std::vector<outlined_item> items;
    };

    // Every screen in the files, in the order they're written.
    std::vector<outlined_screen> screens_of(const std::vector<language::file>& files);

    std::optional<outline> outline_of(const std::vector<language::file>& files);

} // namespace one::driver
