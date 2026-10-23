// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Writes the Go backend for a project: one package per namespace, holding its
// entities, commands, views and roles as a few declarations on the one library,
// and a main.go that serves them all.
//
// The library doesn't do everything the language can say yet. Anything it can't do
// stops the build with "not supported yet" and the line it's on, because Go that
// compiles but does the wrong thing is worse than no Go at all.

#pragma once

#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "generators/javascript.hpp"
#include "generators/output.hpp"
#include "language/ast.hpp"
#include "language/diagnostics.hpp"

namespace one::generators {

    namespace api_detail {

        // A Go name from a snake_case one: created_at becomes CreatedAt, and the usual
        // initialisms stay capitals, so owner_id becomes OwnerID.
        inline std::string go_name(std::string_view snake) {
            static const std::set<std::string, std::less<>> initialisms{"id", "uid", "url", "api", "http", "json", "html"};
            std::string out;
            std::size_t at = 0;
            while (at <= snake.size()) {
                std::size_t end = snake.find('_', at);
                std::string_view word = snake.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
                if (initialisms.contains(word)) {
                    for (char c : word) out += static_cast<char>(c - 'a' + 'A');
                } else if (!word.empty()) {
                    out += static_cast<char>(word[0] >= 'a' && word[0] <= 'z' ? word[0] - 'a' + 'A' : word[0]);
                    out += word.substr(1);
                }
                if (end == std::string_view::npos) break;
                at = end + 1;
            }
            return out;
        }

        inline std::string go_string(std::string_view text) { return web_detail::js_string(text); }
        // at most 30 per hour: the hour, as Go's time says it.
        inline std::string duration(std::string_view per) {
            if (per == "second") return "time.Second";
            if (per == "minute") return "time.Minute";
            return "time.Hour";
        }

        // Pads text to a width, as gofmt aligns struct fields and constants.
        inline std::string pad(const std::string& text, std::size_t width) {
            return text + std::string(width > text.size() ? width - text.size() : 0, ' ');
        }

    } // namespace api_detail

    struct generated_api {
        std::vector<output_file> files;  // inside the api folder, like waitlist/waitlist.go
        language::diagnostics errors;        // what isn't supported yet; nothing is written if any
    };

    // The Go backend for a project whose files have parsed and checked cleanly.
    generated_api generate_api(const std::vector<language::file>& files, const std::string& project_dir, const std::string& out_dir);

} // namespace one::generators
