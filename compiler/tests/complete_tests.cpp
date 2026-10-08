// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What an editor offers as it's typed, from the library: a project's settings in
// its block, an environment's in one, and an enum setting's choices after its name,
// even while the file is half written.

#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include "driver.hpp"

using namespace one;

namespace {

    const driver::completion_item* item(const driver::completions& c, const std::string& label) {
        auto found = std::find_if(c.items.begin(), c.items.end(), [&](const auto& i) { return i.label == label; });
        return found == c.items.end() ? nullptr : &*found;
    }

} // namespace

TEST_CASE("a project's block offers its settings, each with its type and what it's for") {
    std::string text = "import one\n\nproject shop {\n\tui  radix\n\tco\n";
    auto c = driver::complete(text, 5, 4);
    CHECK(c.from == 2);
    auto corners = item(c, "corners");
    REQUIRE(corners);
    CHECK(corners->detail == "corners");
    CHECK(corners->info == "How corners are drawn.");
    REQUIRE(item(c, "signin"));
    CHECK(item(c, "signin")->detail == "list of signin");
    CHECK(item(c, "environment"));
    CHECK_FALSE(item(c, "firebase_project"));
}

TEST_CASE("after an enum setting's name, its choices, each as it's shown") {
    std::string text = "import one\nproject shop {\n\tcorners  sq\n}\n";
    auto c = driver::complete(text, 3, 13);
    CHECK(c.from == 11);
    REQUIRE(c.items.size() == 2);
    CHECK(c.items[0].label == "square");
    CHECK(c.items[0].detail == "Square");
    auto qualified = driver::complete("project shop {\n\tsignin  one::signin::\n", 2, 23);
    CHECK(qualified.from == 23);
    CHECK(item(qualified, "github"));
}

TEST_CASE("an environment offers its own settings, and elsewhere nothing but the import") {
    std::string text = "project shop {\n\tenvironment staging {\n\t\t\n\t}\n}\nentity order {\n\t\n}\n";
    auto environment = driver::complete(text, 3, 3);
    CHECK(item(environment, "firebase"));
    CHECK_FALSE(item(environment, "theme"));
    CHECK(driver::complete(text, 7, 2).items.empty());
    auto top = driver::complete(text, 6, 1);
    CHECK(top.items.size() == 1);
    CHECK(top.items[0].label == "import one");
    // A string with a brace in it doesn't open a block.
    CHECK(item(driver::complete("project shop {\n\ttitle  \"a { b\"\n\t\n", 3, 2), "theme"));
}
