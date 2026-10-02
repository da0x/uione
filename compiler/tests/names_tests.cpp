// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include <doctest/doctest.h>

#include "language/names.hpp"

using namespace one::language;

TEST_CASE("what counts as snake_case") {
    CHECK(is_snake_case("book"));
    CHECK(is_snake_case("due_at"));
    CHECK(is_snake_case("sort_title"));
    CHECK(is_snake_case("h2"));
    CHECK(is_snake_case("line_2_total"));

    CHECK_FALSE(is_snake_case(""));
    CHECK_FALSE(is_snake_case("dueAt"));
    CHECK_FALSE(is_snake_case("DueAt"));
    CHECK_FALSE(is_snake_case("due__at"));
    CHECK_FALSE(is_snake_case("_due"));
    CHECK_FALSE(is_snake_case("due_"));
    CHECK_FALSE(is_snake_case("2nd"));
    CHECK_FALSE(is_snake_case("BOOK"));
}

TEST_CASE("the snake_case spelling of other styles") {
    CHECK(to_snake_case("dueAt") == "due_at");
    CHECK(to_snake_case("DueAt") == "due_at");
    CHECK(to_snake_case("OnShelf") == "on_shelf");
    CHECK(to_snake_case("sortTitle") == "sort_title");
    CHECK(to_snake_case("due_at") == "due_at");
    CHECK(to_snake_case("due-at") == "due_at");
    CHECK(to_snake_case("BOOK") == "book");
    CHECK(to_snake_case("h2Title") == "h2_title");
    CHECK(to_snake_case("__due__at_") == "due_at");
}

TEST_CASE("a run of capitals is one word") {
    CHECK(to_snake_case("HTTPServer") == "http_server");
    CHECK(to_snake_case("parseHTTPResponse") == "parse_http_response");
    CHECK(to_snake_case("userID") == "user_id");
}
