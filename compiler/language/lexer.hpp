// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "language/diagnostics.hpp"
#include "language/token.hpp"

namespace one::language {

    // Turns a .one file into tokens. Line breaks matter in uione, since a field or a
    // statement ends at the end of its line, so they come out as newline tokens. Blank
    // lines and comments collapse into a single one. The last token is always a
    // newline followed by end_of_file.
    class lexer {
    public:
        lexer(std::string_view path, std::string_view source, diagnostics& out)
            : path_(path), source_(source), out_(out) {}

        std::vector<token> tokens() {
            check_indentation();
            std::vector<token> result;
            for (;;) {
                skip_spaces_and_comments(result);
                if (done()) break;
                if (auto t = next()) result.push_back(std::move(*t));
            }
            add_newline(result, here(), pos_);
            result.push_back({token_kind::end_of_file, "", here(), pos_, pos_});
            return result;
        }

    private:
        std::string path_;
        std::string_view source_;
        diagnostics& out_;
        std::size_t pos_ = 0;
        location at_;

        bool done() const { return pos_ >= source_.size(); }
        char current() const { return done() ? '\0' : source_[pos_]; }
        char following() const { return pos_ + 1 < source_.size() ? source_[pos_ + 1] : '\0'; }
        location here() const { return at_; }

        void step() {
            if (current() == '\n') {
                ++at_.line;
                at_.column = 1;
            } else {
                ++at_.column;
            }
            ++pos_;
        }

        void error(location where, std::string message) {
            out_.push_back({path_, where, std::move(message)});
        }

        static bool is_letter(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        }
        static bool is_digit(char c) { return c >= '0' && c <= '9'; }

        static void add_newline(std::vector<token>& tokens, location where, std::size_t offset) {
            if (tokens.empty() || tokens.back().kind == token_kind::newline) return;
            tokens.push_back({token_kind::newline, "", where, offset, offset});
        }

        // Lines are indented with tabs, and only tabs. A tab's width is then up to
        // whoever reads the file, as a name's style is (decisions 0005 and 0006).
        // Spaces after the first word, which line things up, are fine, and so is a
        // line inside a /* */ comment.
        void check_indentation() {
            bool in_comment = false;
            int line = 1;
            for (std::size_t at = 0; at < source_.size();) {
                std::size_t end = source_.find('\n', at);
                if (end == std::string_view::npos) end = source_.size();
                std::string_view text = source_.substr(at, end - at);
                std::size_t first = text.find_first_not_of(" \t\r");
                if (!in_comment && first != std::string_view::npos) {
                    std::size_t space = text.substr(0, first).find(' ');
                    if (space != std::string_view::npos) {
                        error({line, static_cast<int>(space) + 1},
                              "indent with tabs, not spaces: how wide a tab looks is up to whoever reads the file");
                    }
                }
                for (std::size_t i = 0; i + 1 < text.size(); ++i) {
                    if (!in_comment && text[i] == '/' && text[i + 1] == '/') break;
                    if (!in_comment && text[i] == '/' && text[i + 1] == '*') { in_comment = true; ++i; }
                    else if (in_comment && text[i] == '*' && text[i + 1] == '/') { in_comment = false; ++i; }
                }
                at = end + 1;
                ++line;
            }
        }

        void skip_spaces_and_comments(std::vector<token>& tokens) {
            while (!done()) {
                char c = current();
                if (c == ' ' || c == '\t' || c == '\r') {
                    step();
                } else if (c == '\n') {
                    location where = here();
                    std::size_t begin = pos_;
                    step();
                    if (!tokens.empty() && tokens.back().kind != token_kind::newline) {
                        tokens.push_back({token_kind::newline, "", where, begin, pos_});
                    }
                } else if (c == '/' && following() == '/') {
                    while (!done() && current() != '\n') step();
                } else if (c == '/' && following() == '*') {
                    location where = here();
                    step();
                    step();
                    bool spans_lines = false;
                    while (!done() && !(current() == '*' && following() == '/')) {
                        spans_lines |= current() == '\n';
                        step();
                    }
                    if (done()) {
                        error(where, "this comment is never closed; end it with */");
                    } else {
                        step();
                        step();
                    }
                    if (spans_lines) add_newline(tokens, where, pos_);
                } else {
                    return;
                }
            }
        }

        token make(token_kind kind, std::string text, location where, std::size_t begin) {
            return {kind, std::move(text), where, begin, pos_};
        }

        // The next token, or nothing when the character there isn't part of the
        // language. That's reported and skipped, so one bad character is one error.
        std::optional<token> next() {
            location where = here();
            std::size_t begin = pos_;
            char c = current();

            if (is_letter(c)) {
                while (is_letter(current()) || is_digit(current())) step();
                return make(token_kind::identifier, std::string(source_.substr(begin, pos_ - begin)), where, begin);
            }
            if (is_digit(c)) {
                while (is_digit(current())) step();
                if (current() == '.' && is_digit(following())) {
                    step();
                    while (is_digit(current())) step();
                }
                return make(token_kind::number, std::string(source_.substr(begin, pos_ - begin)), where, begin);
            }
            if (c == '"') return string_literal(where, begin);
            if (c == '/') return route(where, begin);
            if (c == '#' && is_letter(following())) {
                step();
                std::size_t name = pos_;
                while (is_letter(current()) || is_digit(current()) || current() == '-') step();
                return make(token_kind::anchor, std::string(source_.substr(name, pos_ - name)), where, begin);
            }

            step();
            switch (c) {
                case '{': return make(token_kind::left_brace, "{", where, begin);
                case '}': return make(token_kind::right_brace, "}", where, begin);
                case '(': return make(token_kind::left_paren, "(", where, begin);
                case ')': return make(token_kind::right_paren, ")", where, begin);
                case '[': return make(token_kind::left_bracket, "[", where, begin);
                case ']': return make(token_kind::right_bracket, "]", where, begin);
                case ',': return make(token_kind::comma, ",", where, begin);
                case '.': return make(token_kind::dot, ".", where, begin);
                case '+': return make(token_kind::plus, "+", where, begin);
                case '-': return make(token_kind::minus, "-", where, begin);
                case '*': return make(token_kind::star, "*", where, begin);
                case ':':
                    if (current() == ':') { step(); return make(token_kind::scope, "::", where, begin); }
                    break;
                case '=':
                    if (current() == '=') { step(); return make(token_kind::equal, "==", where, begin); }
                    return make(token_kind::assign, "=", where, begin);
                case '!':
                    if (current() == '=') { step(); return make(token_kind::not_equal, "!=", where, begin); }
                    return make(token_kind::logical_not, "!", where, begin);
                case '<':
                    if (current() == '=') { step(); return make(token_kind::less_equal, "<=", where, begin); }
                    return make(token_kind::less, "<", where, begin);
                case '>':
                    if (current() == '=') { step(); return make(token_kind::greater_equal, ">=", where, begin); }
                    return make(token_kind::greater, ">", where, begin);
                case '&':
                    if (current() == '&') { step(); return make(token_kind::logical_and, "&&", where, begin); }
                    break;
                case '|':
                    if (current() == '|') { step(); return make(token_kind::logical_or, "||", where, begin); }
                    return make(token_kind::pipe, "|", where, begin);
                default:
                    break;
            }
            error(where, "'" + std::string(1, c) + "' isn't part of the language here");
            return std::nullopt;
        }

        token string_literal(location where, std::size_t begin) {
            step();
            std::string value;
            while (!done() && current() != '"' && current() != '\n') {
                if (current() == '\\' && (following() == '"' || following() == '\\')) {
                    step();
                }
                value += current();
                step();
            }
            if (current() == '"') {
                step();
            } else {
                error(where, "this string is never closed; end it with \" on the same line");
            }
            return make(token_kind::string, std::move(value), where, begin);
        }

        // A route starts with / and is followed by its path, a parameter like :page,
        // or nothing at all when it's the root. Its last parameter may take the rest
        // of the address, slashes and all, like :file* in /code/:file*, and a link's
        // address can name a file, like /code/components/chart.tsx.
        token route(location where, std::size_t begin) {
            step();
            while (is_letter(current()) || is_digit(current()) || current() == '/' ||
                   current() == ':' || current() == '-' || current() == '*' || current() == '.') {
                step();
            }
            return make(token_kind::route, std::string(source_.substr(begin, pos_ - begin)), where, begin);
        }
    };

} // namespace one::language
