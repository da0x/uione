// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace one::language {

    enum class token_kind {
        identifier,
        string,
        number,
        route,          // /shelf, /docs/:page, or just /
        anchor,         // #waitlist, stored without the #
        left_brace,
        right_brace,
        left_paren,
        right_paren,
        left_bracket,
        right_bracket,
        comma,
        dot,
        scope,          // ::
        assign,         // =
        equal,          // ==
        not_equal,      // !=
        less,
        greater,
        less_equal,
        greater_equal,
        logical_and,    // &&
        logical_or,     // ||
        logical_not,    // !
        plus,
        minus,
        star,
        has,            // labels has bug: written as a word, made by the parser
        pipe,           // | between choices
        newline,
        end_of_file,
    };

    // Where something is in a source file. Lines and columns start at 1.
    struct location {
        int line = 1;
        int column = 1;
    };

    struct token {
        token_kind kind = token_kind::end_of_file;
        std::string text;       // the name, the string's contents, the number, the route
        location where;
        std::size_t begin = 0;  // byte offsets into the source, for the few places the
        std::size_t end = 0;    // parser reads raw text, like a format's pattern
    };

    // How a kind of token is named in an error message.
    inline std::string_view describe(token_kind kind) {
        switch (kind) {
            case token_kind::identifier:    return "a name";
            case token_kind::string:        return "a string";
            case token_kind::number:        return "a number";
            case token_kind::route:         return "a route";
            case token_kind::anchor:        return "an anchor";
            case token_kind::left_brace:    return "'{'";
            case token_kind::right_brace:   return "'}'";
            case token_kind::left_paren:    return "'('";
            case token_kind::right_paren:   return "')'";
            case token_kind::left_bracket:  return "'['";
            case token_kind::right_bracket: return "']'";
            case token_kind::comma:         return "','";
            case token_kind::dot:           return "'.'";
            case token_kind::scope:         return "'::'";
            case token_kind::assign:        return "'='";
            case token_kind::equal:         return "'=='";
            case token_kind::not_equal:     return "'!='";
            case token_kind::less:          return "'<'";
            case token_kind::greater:       return "'>'";
            case token_kind::less_equal:    return "'<='";
            case token_kind::greater_equal: return "'>='";
            case token_kind::logical_and:   return "'&&'";
            case token_kind::logical_or:    return "'||'";
            case token_kind::logical_not:   return "'!'";
            case token_kind::plus:          return "'+'";
            case token_kind::minus:         return "'-'";
            case token_kind::star:          return "'*'";
            case token_kind::has:           return "'has'";
            case token_kind::pipe:          return "'|'";
            case token_kind::newline:       return "the end of the line";
            case token_kind::end_of_file:   return "the end of the file";
        }
        return "a token";
    }

} // namespace one::language
