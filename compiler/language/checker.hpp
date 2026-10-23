// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The checker looks at what a project's .one files mean, after they've parsed. A file
// can be well-formed and still wrong: a name in camelCase, a command on an entity
// nobody declared, a table showing a view that doesn't exist.
//
// It reads a whole project at once, because one file can use what another declares.
// It goes through the files twice: first to collect every declaration by namespace,
// then to check each one against that collection.
//
// Names inside expressions are checked for snake_case, but not yet looked up. That
// needs a model of scopes and types, which will come with the generators.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "language/ast.hpp"
#include "language/diagnostics.hpp"
#include "language/token.hpp"

namespace one::language {

    // What a name written in a file means: what it names, said in words, and where
    // that's declared, or the reference's section for one of the language's own.
    // An editor shows it on hover, and goes to it.
    struct meaning {
        std::string path;        // the file the name is written in
        location where;          // where it starts
        std::size_t length = 0;
        std::string says;        // field project of report, a project
        std::string to_path;     // the file it's declared in; empty for the language's own
        location to;
        std::string section;     // the reference's section, like built-in-values
    };
    using meanings = std::vector<meaning>;

    // What the library says of what's declared on a line of it, from the comments just
    // above, like what a setting is for, or "" when nothing is said.
    std::string library_comment(int line);

    // Checks what a project's files mean, once what they say in short is said in full.
    // Call it only after every file has parsed without errors, since a half-read file
    // would only produce follow-on errors.
    void check(std::vector<file>& files, diagnostics& out);

    // Checks, and says what each name means where it's written.
    void check(std::vector<file>& files, diagnostics& out, meanings& meant);

} // namespace one::language
