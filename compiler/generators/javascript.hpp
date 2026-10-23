// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Text and names as JavaScript and HTML write them.

#pragma once

#include <algorithm>
#include <string>
#include <string_view>

#include "language/names.hpp"

namespace one::generators {

    namespace web_detail {

        // Text as HTML, with what would be markup written as entities.
        inline std::string html_escape(std::string_view text) {
            std::string out;
            for (char c : text) {
                switch (c) {
                    case '&': out += "&amp;"; break;
                    case '<': out += "&lt;"; break;
                    case '>': out += "&gt;"; break;
                    case '"': out += "&quot;"; break;
                    default: out += c;
                }
            }
            return out;
        }

        // A string as JavaScript source: "like this", with quotes and backslashes escaped.
        inline std::string js_string(std::string_view text) {
            std::string out = "\"";
            for (char c : text) {
                switch (c) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default: out += c;
                }
            }
            return out + "\"";
        }

        // Text as JSX children. Braces and angle brackets would be read as code, so
        // they're written as string expressions.
        inline std::string jsx_text(std::string_view text) {
            std::string out;
            for (char c : text) {
                if (c == '{' || c == '}' || c == '<' || c == '>') {
                    out += "{\"";
                    out += c;
                    out += "\"}";
                } else {
                    out += c;
                }
            }
            return out;
        }

        // A JavaScript name from a .one name or a title: sort_title and "Sort title"
        // both become sortTitle.
        // What a way of signing in is called in an app: signInWithGitHub for github.
        inline std::string sign_in_with(std::string_view method) {
            if (method == "github") return "signInWithGitHub";
            std::string name(method);
            name[0] = static_cast<char>(name[0] - 'a' + 'A');
            return "signInWith" + name;
        }

        inline std::string js_name(std::string_view name) {
            std::string snake = language::to_snake_case(name);
            std::string out;
            bool upper = false;
            for (char c : snake) {
                bool letter_or_digit = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
                if (!letter_or_digit) {
                    upper = !out.empty();
                    continue;
                }
                out += upper && c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
                upper = false;
            }
            if (out.empty() || (out[0] >= '0' && out[0] <= '9')) out = "screen" + out;
            return out;
        }

        inline bool is_identifier(std::string_view key) {
            if (key.empty() || (key[0] >= '0' && key[0] <= '9')) return false;
            return std::all_of(key.begin(), key.end(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
            });
        }

    } // namespace web_detail

} // namespace one::generators
