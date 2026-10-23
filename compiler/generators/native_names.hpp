// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Text and names as C++ writes them, for the native apps and the agents.

#pragma once

#include <cctype>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace one::generators {

    namespace native_detail {

        // Text as a C++ string literal, quotes, backslashes and control characters escaped.
        inline std::string literal(std::string_view text) {
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

        // A name C++ takes as an identifier: snake_case as .one names are, never a keyword.
        inline std::string identifier(std::string_view name) {
            std::string out;
            for (char c : name) out += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '_';
            static const std::set<std::string> taken{"and", "or", "not", "new", "delete", "class", "default", "this", "operator", "template", "export",
                                                      "register", "union", "auto", "case", "char", "const", "do", "double", "else", "enum", "float", "for",
                                                      "if", "int", "long", "return", "short", "signed", "static", "struct", "switch", "typedef", "unsigned",
                                                      "void", "volatile", "while", "page", "site", "screen", "view", "value", "state"};
            if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0])) || taken.contains(out)) out += "_";
            return out;
        }

        inline std::string strings(const std::vector<std::string>& all) {
            std::string out;
            for (const auto& s : all) out += (out.empty() ? "" : ", ") + literal(s);
            return "{" + out + "}";
        }

    }  // namespace native_detail

} // namespace one::generators
