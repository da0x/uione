// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Holds the parser to the written grammar in docs/grammar.ebnf, both ways: every
// program made from the grammar's rules parses, and every .one file in the
// repository fits the rules. Either failing means the two have drifted apart.

#include <doctest/doctest.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "language/lexer.hpp"
#include "language/parser.hpp"
#include "platform/files.hpp"

using namespace one;

namespace {

    const std::string root = UIONE_ROOT;

    // A grammar, as read from the EBNF.
    struct node {
        enum class kind { sequence, choice, word, piece, rule, any, some, maybe };
        kind type = kind::sequence;
        std::string text;  // the word, the piece (NAME, STRING, ...) or the rule's name
        std::vector<node> parts;
    };

    using grammar = std::map<std::string, node>;

    // reading the EBNF

    struct ebnf_reader {
        std::vector<std::string> tokens;
        std::size_t at = 0;

        explicit ebnf_reader(std::string text) {
            for (std::size_t c; (c = text.find("/*")) != std::string::npos;) {
                std::size_t end = text.find("*/", c + 2);
                text.erase(c, end == std::string::npos ? std::string::npos : end + 2 - c);
            }
            std::size_t i = 0;
            while (i < text.size()) {
                char c = text[i];
                if (std::isspace(static_cast<unsigned char>(c))) {
                    ++i;
                } else if (c == '"') {
                    std::size_t end = text.find('"', i + 1);
                    tokens.push_back(text.substr(i, end + 1 - i));
                    i = end + 1;
                } else if (text.compare(i, 3, "::=") == 0) {
                    tokens.push_back("::=");
                    i += 3;
                } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                    std::size_t start = i;
                    while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_')) ++i;
                    tokens.push_back(text.substr(start, i - start));
                } else {
                    tokens.push_back(std::string(1, c));
                    ++i;
                }
            }
        }

        bool rule_starts_here() const { return at + 1 < tokens.size() && tokens[at + 1] == "::="; }

        grammar read() {
            grammar g;
            while (at < tokens.size()) {
                std::string name = tokens[at];
                at += 2;
                g[name] = choice();
            }
            return g;
        }

        node choice() {
            node n{node::kind::choice, "", {sequence()}};
            while (at < tokens.size() && tokens[at] == "|") {
                ++at;
                n.parts.push_back(sequence());
            }
            return n.parts.size() == 1 ? n.parts[0] : n;
        }

        node sequence() {
            node n{node::kind::sequence, "", {}};
            while (at < tokens.size() && tokens[at] != "|" && tokens[at] != ")" && !rule_starts_here()) {
                n.parts.push_back(repeated());
            }
            return n.parts.size() == 1 ? n.parts[0] : n;
        }

        node repeated() {
            node n = atom();
            if (at < tokens.size()) {
                const std::string& t = tokens[at];
                node::kind k = t == "*" ? node::kind::any : t == "+" ? node::kind::some : node::kind::maybe;
                if (t == "*" || t == "+" || t == "?") {
                    ++at;
                    return node{k, "", {std::move(n)}};
                }
            }
            return n;
        }

        node atom() {
            const std::string t = tokens[at++];
            if (t == "(") {
                node n = choice();
                ++at;  // )
                return n;
            }
            if (t.front() == '"') return node{node::kind::word, t.substr(1, t.size() - 2), {}};
            if (std::isupper(static_cast<unsigned char>(t.front()))) return node{node::kind::piece, t, {}};
            return node{node::kind::rule, t, {}};
        }
    };

    grammar read_grammar() {
        auto text = platform::read_file(root + "/docs/grammar.ebnf");
        REQUIRE(text);
        return ebnf_reader(*text).read();
    }

    // recognizing a file's tokens

    struct recognizer {
        const grammar& rules;
        const std::vector<language::token>& tokens;
        std::map<std::pair<std::string, std::size_t>, std::vector<std::size_t>> memo;

        using token_kind = language::token_kind;

        bool punctuation(const language::token& t) const {
            return t.kind != token_kind::string && t.kind != token_kind::number && t.kind != token_kind::route &&
                   t.kind != token_kind::anchor && t.kind != token_kind::newline && t.kind != token_kind::end_of_file;
        }

        // Every place in the tokens where `n` can end, starting at `at`.
        std::vector<std::size_t> ends(const node& n, std::size_t at) {
            std::vector<std::size_t> out;
            auto add = [&](std::size_t e) {
                if (std::find(out.begin(), out.end(), e) == out.end()) out.push_back(e);
            };
            switch (n.type) {
                case node::kind::word:
                    if (at < tokens.size() && punctuation(tokens[at]) && tokens[at].text == n.text) add(at + 1);
                    break;
                case node::kind::piece: {
                    if (at >= tokens.size()) break;
                    const auto& t = tokens[at];
                    const std::string& p = n.text;
                    if ((p == "NAME" && t.kind == token_kind::identifier) || (p == "STRING" && t.kind == token_kind::string) ||
                        (p == "NUMBER" && t.kind == token_kind::number) || (p == "ROUTE" && t.kind == token_kind::route) ||
                        (p == "WHOLE" && t.kind == token_kind::number && t.text.find('.') == std::string::npos) ||
                        (p == "ANCHOR" && t.kind == token_kind::anchor) || (p == "NEWLINE" && t.kind == token_kind::newline) ||
                        (p == "EOF" && t.kind == token_kind::end_of_file)) {
                        add(at + 1);
                    } else if (p == "END") {
                        if (t.kind == token_kind::newline) add(at + 1);
                        if (t.kind == token_kind::right_brace || t.kind == token_kind::end_of_file) add(at);
                    } else if (p == "PATTERN") {
                        std::size_t e = at;
                        while (e < tokens.size() && tokens[e].kind != token_kind::newline && tokens[e].kind != token_kind::left_brace &&
                               tokens[e].kind != token_kind::end_of_file) {
                            ++e;
                        }
                        if (e > at) add(e);
                    }
                    break;
                }
                case node::kind::rule: {
                    auto key = std::pair{n.text, at};
                    if (auto it = memo.find(key); it != memo.end()) return it->second;
                    memo[key] = {};  // a rule that comes back to itself without reading anything matches nothing
                    auto found = rules.find(n.text);
                    if (found == rules.end()) throw std::runtime_error("the grammar has no rule " + n.text);
                    out = ends(found->second, at);
                    memo[key] = out;
                    break;
                }
                case node::kind::sequence: {
                    std::vector<std::size_t> here{at};
                    for (const auto& part : n.parts) {
                        std::vector<std::size_t> next;
                        for (std::size_t h : here) {
                            for (std::size_t e : ends(part, h)) {
                                if (std::find(next.begin(), next.end(), e) == next.end()) next.push_back(e);
                            }
                        }
                        here = std::move(next);
                    }
                    out = std::move(here);
                    break;
                }
                case node::kind::choice:
                    for (const auto& part : n.parts) {
                        for (std::size_t e : ends(part, at)) add(e);
                    }
                    break;
                case node::kind::maybe:
                    add(at);
                    for (std::size_t e : ends(n.parts[0], at)) add(e);
                    break;
                case node::kind::any:
                case node::kind::some: {
                    std::vector<std::size_t> frontier{at};
                    if (n.type == node::kind::any) add(at);
                    std::set<std::size_t> seen{at};
                    while (!frontier.empty()) {
                        std::vector<std::size_t> next;
                        for (std::size_t f : frontier) {
                            for (std::size_t e : ends(n.parts[0], f)) {
                                add(e);
                                if (seen.insert(e).second) next.push_back(e);
                            }
                        }
                        frontier = std::move(next);
                    }
                    break;
                }
            }
            return out;
        }
    };

    bool fits(const grammar& g, const std::string& source) {
        language::diagnostics problems;
        auto tokens = language::lexer("test.one", source, problems).tokens();
        if (!problems.empty()) return false;
        recognizer r{g, tokens, {}};
        auto ends = r.ends(node{node::kind::rule, "file", {}}, 0);
        return std::find(ends.begin(), ends.end(), tokens.size()) != ends.end();
    }

    // making programs from the rules

    struct generator {
        const grammar& rules;
        std::mt19937 random;
        std::map<std::string, int> cost;  // the fewest pieces each rule can be made of

        generator(const grammar& g, unsigned seed) : rules(g), random(seed) {
            for (int round = 0; round < 50; ++round) {
                for (const auto& [name, n] : rules) cost[name] = smallest(n);
            }
        }

        int smallest(const node& n) {
            switch (n.type) {
                case node::kind::word:
                case node::kind::piece: return 1;
                case node::kind::rule: return cost.contains(n.text) ? cost[n.text] : 1000;
                case node::kind::any:
                case node::kind::maybe: return 0;
                case node::kind::some: return smallest(n.parts[0]);
                case node::kind::sequence: {
                    int total = 0;
                    for (const auto& p : n.parts) total += smallest(p);
                    return std::min(total, 1000);
                }
                case node::kind::choice: {
                    int best = 1000;
                    for (const auto& p : n.parts) best = std::min(best, smallest(p));
                    return best;
                }
            }
            return 1000;
        }

        int below(int n) { return std::uniform_int_distribution<int>(0, n - 1)(random); }

        void make(const node& n, int depth, std::vector<std::string>& out) {
            bool deep = depth > 7;  // past this, take the shortest way out
            switch (n.type) {
                case node::kind::word: out.push_back(n.text); break;
                case node::kind::piece: {
                    static const std::map<std::string, std::vector<std::string>> samples{
                        {"NAME", {"alpha", "beta", "gamma", "delta_one"}}, {"STRING", {"\"words\"", "\"two words\""}},
                        {"NUMBER", {"42", "2.5"}}, {"WHOLE", {"20", "50"}}, {"ROUTE", {"/", "/shelf", "/docs/:page"}}, {"ANCHOR", {"#top"}},
                        {"PATTERN", {"AAA-9999"}}, {"NEWLINE", {"\n"}}, {"END", {"\n"}}, {"EOF", {""}}};
                    const auto& choices = samples.at(n.text);
                    out.push_back(choices[static_cast<std::size_t>(below(static_cast<int>(choices.size())))]);
                    break;
                }
                case node::kind::rule: make(rules.at(n.text), depth + 1, out); break;
                case node::kind::sequence:
                    for (const auto& p : n.parts) make(p, depth, out);
                    break;
                case node::kind::choice: {
                    if (deep) {
                        const node* best = &n.parts[0];
                        for (const auto& p : n.parts) {
                            if (smallest(p) < smallest(*best)) best = &p;
                        }
                        make(*best, depth, out);
                    } else {
                        make(n.parts[static_cast<std::size_t>(below(static_cast<int>(n.parts.size())))], depth, out);
                    }
                    break;
                }
                case node::kind::maybe:
                    if (!deep && below(2)) make(n.parts[0], depth, out);
                    break;
                case node::kind::any:
                case node::kind::some: {
                    int times = deep ? 0 : below(3);
                    if (n.type == node::kind::some) times = std::max(times, 1);
                    for (int i = 0; i < times; ++i) make(n.parts[0], depth, out);
                    break;
                }
            }
        }

        std::string program() {
            std::vector<std::string> pieces;
            make(node{node::kind::rule, "file", {}}, 0, pieces);
            std::string text;
            for (const auto& p : pieces) {
                if (!text.empty() && text.back() != '\n' && p != "\n" && !p.empty()) text += ' ';
                text += p;
            }
            return text;
        }
    };

} // namespace

TEST_CASE("every program made from the grammar parses") {
    auto g = read_grammar();
    generator make(g, 20260930);
    int made = 0;
    for (int tries = 0; made < 300 && tries < 3000; ++tries) {
        std::string program = make.program();
        if (program.empty()) continue;
        ++made;
        CAPTURE(program);
        language::diagnostics out;
        language::parse("made.one", program, out);
        for (const auto& d : out) FAIL_CHECK(language::format(d));
        CHECK(fits(g, program));  // and the grammar recognizes what it made
    }
    CHECK(made == 300);
}

TEST_CASE("every .one file in the repository fits the grammar") {
    auto g = read_grammar();
    for (const char* project : {"/site", "/examples/tasks", "/examples/library", "/examples/tracker"}) {
        for (const auto& path : platform::find_one_files({root + project})) {
            CAPTURE(path);
            CHECK(fits(g, *platform::read_file(path)));
        }
    }
}

TEST_CASE("the grammar turns away what the language doesn't allow") {
    auto g = read_grammar();
    CHECK(fits(g, "entity book {\n\ttitle  text  required\n}\n"));
    CHECK_FALSE(fits(g, "entity book {\n\ttitle\n}\n"));                      // a field needs a type
    CHECK_FALSE(fits(g, "fn sort(title) {\n\treturn title\n}\n"));            // functions are declared with function
    CHECK_FALSE(fits(g, "project p {\n\tcolour \"blue\"\n}\n"));              // not a setting
    CHECK_FALSE(fits(g, "entity book {\n\ttitle  text\n"));                   // never closed
}
