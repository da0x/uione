// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Every project in the repository has to parse and check without errors. The
// examples and the site are the language as it's really written, so they're the test
// that matters most.

#include <doctest/doctest.h>

#include <string>

#include "language/checker.hpp"
#include "language/parser.hpp"
#include "platform/files.hpp"

using namespace one::language;

TEST_CASE("every example and the site check without errors") {
    std::string root = UIONE_ROOT;
    for (const char* project : {"/examples/library", "/examples/tasks", "/examples/tracker", "/site"}) {
        CAPTURE(project);
        auto paths = one::platform::find_one_files({root + project});
        REQUIRE_FALSE(paths.empty());
        diagnostics out;
        std::vector<file> files;
        for (const auto& path : paths) {
            auto source = one::platform::read_file(path);
            REQUIRE(source);
            files.push_back(parse(path, *source, out));
            CHECK_FALSE(files.back().declarations.empty());
        }
        if (out.empty()) check(files, out);
        for (const auto& d : out) FAIL_CHECK(format(d));
    }
}
