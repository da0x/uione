// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Every name in a .one file is snake_case (decision 0006). These are the rules for
// what counts as snake_case, and the conversion used to suggest a fix. The studio
// will need the same conversions when it shows names in each reader's style.

#pragma once

#include <string>
#include <string_view>

namespace one::language {

    namespace detail {
        inline bool is_lower(char c) { return c >= 'a' && c <= 'z'; }
        inline bool is_upper(char c) { return c >= 'A' && c <= 'Z'; }
        inline bool is_digit(char c) { return c >= '0' && c <= '9'; }
    } // namespace detail

    // Lowercase letters and digits, starting with a letter, with single underscores
    // between words: due_at, sort_title, h2.
    inline bool is_snake_case(std::string_view name) {
        if (name.empty() || !detail::is_lower(name.front()) || name.back() == '_') return false;
        char previous = '\0';
        for (char c : name) {
            if (c == '_' && previous == '_') return false;
            if (c != '_' && !detail::is_lower(c) && !detail::is_digit(c)) return false;
            previous = c;
        }
        return true;
    }

    // The snake_case spelling of a name written another way. A capital starts a new
    // word, and a run of capitals is one word, so dueAt becomes due_at and
    // parseHTTPResponse becomes parse_http_response.
    inline std::string to_snake_case(std::string_view name) {
        std::string words;
        for (std::size_t i = 0; i < name.size(); ++i) {
            char c = name[i];
            if (detail::is_upper(c)) {
                bool after_word = i > 0 && (detail::is_lower(name[i - 1]) || detail::is_digit(name[i - 1]));
                bool ends_a_run = i > 0 && detail::is_upper(name[i - 1]) &&
                                  i + 1 < name.size() && detail::is_lower(name[i + 1]);
                if (after_word || ends_a_run) words += '_';
                words += static_cast<char>(c - 'A' + 'a');
            } else if (c == '-' || c == ' ') {
                words += '_';
            } else {
                words += c;
            }
        }
        std::string result;
        for (char c : words) {
            if (c == '_' && (result.empty() || result.back() == '_')) continue;
            result += c;
        }
        if (!result.empty() && result.back() == '_') result.pop_back();
        return result;
    }

} // namespace one::language
