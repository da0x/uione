// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include <doctest/doctest.h>

#include <vector>

#include "language/lexer.hpp"

using namespace one::language;
using k = token_kind;

namespace {

    std::vector<token> lex(std::string_view source, diagnostics& out) {
        return lexer("test.one", source, out).tokens();
    }

    std::vector<token_kind> kinds(std::string_view source) {
        diagnostics out;
        std::vector<token_kind> result;
        for (const auto& t : lex(source, out)) result.push_back(t.kind);
        CHECK(out.empty());
        return result;
    }

} // namespace

TEST_CASE("names, strings and numbers") {
    diagnostics out;
    auto tokens = lex("title \"The Hobbit\" 4 2.5", out);
    REQUIRE(tokens.size() == 6);
    CHECK(tokens[0].kind == k::identifier);
    CHECK(tokens[0].text == "title");
    CHECK(tokens[1].kind == k::string);
    CHECK(tokens[1].text == "The Hobbit");
    CHECK(tokens[2].text == "4");
    CHECK(tokens[3].text == "2.5");
    CHECK(tokens[4].kind == k::newline);
    CHECK(tokens[5].kind == k::end_of_file);
}

TEST_CASE("blank lines and comments collapse into one line break") {
    CHECK(kinds("a\n\n\n// a comment\nb") == std::vector{k::identifier, k::newline, k::identifier, k::newline, k::end_of_file});
    CHECK(kinds("a /* one\ntwo */ b") == std::vector{k::identifier, k::newline, k::identifier, k::newline, k::end_of_file});
    CHECK(kinds("a /* inline */ b") == std::vector{k::identifier, k::identifier, k::newline, k::end_of_file});
}

TEST_CASE("routes, including the root and parameters") {
    diagnostics out;
    auto tokens = lex("screen \"Home\" / {\nscreen docs /:page {\nlink \"x\" /docs/language", out);
    CHECK(out.empty());
    CHECK(tokens[2].kind == k::route);
    CHECK(tokens[2].text == "/");
    CHECK(tokens[7].kind == k::route);
    CHECK(tokens[7].text == "/:page");
    CHECK(tokens[12].text == "/docs/language");
}

TEST_CASE("anchors keep their name without the #") {
    diagnostics out;
    auto tokens = lex("section \"Join\" #wait-list {", out);
    CHECK(tokens[2].kind == k::anchor);
    CHECK(tokens[2].text == "wait-list");
}

TEST_CASE("operators") {
    CHECK(kinds("== != <= >= && || ! :: | = - + * < >") ==
          std::vector{k::equal, k::not_equal, k::less_equal, k::greater_equal, k::logical_and,
                      k::logical_or, k::logical_not, k::scope, k::pipe, k::assign, k::minus,
                      k::plus, k::star, k::less, k::greater, k::newline, k::end_of_file});
}

TEST_CASE("tokens know where they are") {
    diagnostics out;
    auto tokens = lex("entity book {\n  title text\n}", out);
    CHECK(tokens[4].text == "title");
    CHECK(tokens[4].where.line == 2);
    CHECK(tokens[4].where.column == 3);
}

TEST_CASE("an unclosed string is reported where it starts") {
    diagnostics out;
    lex("text \"never closed\nnext", out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].where.line == 1);
    CHECK(out[0].where.column == 6);
    CHECK(out[0].message.find("never closed") != std::string::npos);
}

TEST_CASE("strings on the lines right after one, and nothing else, go on with it") {
    diagnostics out;
    auto tokens = lex("text \"Words that go\"\n\t\t\"on over lines,\"  // a note\n\t\t\"three of them.\"\nnext", out);
    for (const auto& d : out) FAIL_CHECK(d.message);
    REQUIRE(tokens.size() == 6);
    CHECK(tokens[1].kind == k::string);
    CHECK(tokens[1].text == "Words that go on over lines, three of them.");
    CHECK(tokens[2].kind == k::newline);
    CHECK(tokens[3].text == "next");
}

TEST_CASE("a string followed by more on its line, or a line with more than a string, stands alone") {
    diagnostics out;
    auto tokens = lex("a \"one\" b\n\"two\"\nc \"three\"\n\"four\" d", out);
    CHECK(out.empty());
    std::vector<std::string> strings;
    for (const auto& t : tokens)
        if (t.kind == k::string) strings.push_back(t.text);
    CHECK(strings == std::vector<std::string>{"one", "two", "three", "four"});
}

TEST_CASE("a character that isn't part of the language is one error, then skipped") {
    diagnostics out;
    auto tokens = lex("a @ b", out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].where.column == 3);
    CHECK(tokens[0].text == "a");
    CHECK(tokens[1].text == "b");
}

TEST_CASE("lines are indented with tabs, not spaces") {
    diagnostics out;
    lexer("test.one", "entity book {\n\ttitle  text\n  author  text\n}\n", out).tokens();
    REQUIRE(out.size() == 1);
    CHECK(out[0].where.line == 3);
    CHECK(out[0].message == "indent with tabs, not spaces: how wide a tab looks is up to whoever reads the file");

    diagnostics fine;
    lexer("test.one", "entity book {\n\ttitle   text  required\n/*\n * a comment, lined up with spaces\n */\n}\n", fine).tokens();
    CHECK(fine.empty());
}
