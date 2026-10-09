// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

// A theme as the page draws it: the CSS variables the component set reads, light and
// dark, from the theme's colors, fonts, corners and depth, with everything else
// worked out from them: text on the accent, hover, the soft accent behind a chosen
// row, the edge of a field, grid lines, the tints of how urgent something is, and
// shadows. Nothing worked out is ever written in a theme, so a theme stays its
// roles and can't drift.

#include <map>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "language/ast.hpp"
#include "language/colors.hpp"
#include "language/library.hpp"
#include "language/parser.hpp"

namespace one::generators::theme {

    struct look {
        std::map<std::string, std::pair<std::string, std::string>> colors;  // role: light, dark
        std::string text = "IBM Plex Sans", heading, code = "IBM Plex Mono";
        std::string ground, ground_dark;  // the code editor's, when the theme says
        int corners = 10;
        std::string depth = "raised";
        std::string base;  // the built-in theme it starts from, whose shades its sections take
    };

    namespace detail {

        inline void collect(const std::vector<language::declaration>& declarations, std::map<std::string, const language::theme_declaration*>& out) {
            for (const auto& d : declarations) {
                if (auto* t = std::get_if<language::theme_declaration>(&d.node)) out.emplace(t->name, t);
                else if (auto* n = std::get_if<language::namespace_declaration>(&d.node)) collect(n->declarations, out);
            }
        }

        inline const language::file& library() {
            static const language::file parsed = [] {
                language::diagnostics ignored;
                return language::parse(std::string(language::library_path), language::library_source, ignored);
            }();
            return parsed;
        }

        // A font as CSS names it, with what's shown before it loads or without it.
        inline std::string stack(const std::string& family, bool mono) {
            if (family == "IBM Plex Sans") return "\"IBM Plex Sans Variable\", \"IBM Plex Sans\", ui-sans-serif, system-ui, sans-serif";
            std::string quoted = "\"" + family + "\"";
            return mono ? quoted + ", ui-monospace, \"SFMono-Regular\", Menlo, monospace" : quoted + ", ui-sans-serif, system-ui, sans-serif";
        }

        inline std::string side(const look& l, bool dark) {
            auto c = [&](const std::string& role) { return dark ? l.colors.at(role).second : l.colors.at(role).first; };
            std::string out;
            auto put = [&](const std::string& name, const std::string& value) { out += "  --color-" + name + ": " + value + ";\n"; };
            for (const auto& role : {"page", "surface", "sunken", "ink", "muted", "line", "accent", "danger", "success", "warning"}) put(role, c(role));
            put("accent-ink", language::colors::on(c("accent"), c("page")));
            put("accent-hover", "color-mix(in oklch, " + c("accent") + " 85%, " + c("ink") + ")");
            put("accent-soft", "color-mix(in oklch, " + c("accent") + " 14%, " + c("surface") + ")");
            // The edge of a field stands out from the page by 3:1.
            put("control-line", "color-mix(in oklch, " + c("ink") + " 45%, " + c("page") + ")");
            put("grid", "color-mix(in srgb, " + c("accent") + " 7%, transparent)");
            // How urgent something is, as Trac colored it: the most urgent the danger's,
            // then the warning's, then plain, then what can wait in the theme's own.
            std::map<std::string, std::string> tones{{"1", c("danger")}, {"2", c("warning")}, {"4", c("success")}, {"5", c("accent")}};
            for (const auto& [n, from] : tones) {
                put("tone-" + n, "color-mix(in oklch, " + from + " 16%, " + c("surface") + ")");
                put("tone-" + n + "-edge", "color-mix(in oklch, " + from + " 55%, " + c("surface") + ")");
            }
            if (l.depth == "flat") {
                out += "  --shadow-panel: none;\n";
                out += dark ? "  --shadow-raised: 0 16px 40px -12px rgb(0 0 0 / 0.6);\n" : "  --shadow-raised: 0 8px 24px -8px rgb(0 0 0 / 0.2);\n";
            } else {
                out += dark ? "  --shadow-panel: 0 1px 0 rgb(255 255 255 / 0.03) inset;\n  --shadow-raised: 0 16px 40px -12px rgb(0 0 0 / 0.6);\n"
                            : "  --shadow-panel: 0 1px 2px rgb(14 23 38 / 0.05), 0 1px 1px rgb(14 23 38 / 0.03);\n"
                              "  --shadow-raised: 0 12px 32px -8px rgb(14 23 38 / 0.18), 0 2px 6px rgb(14 23 38 / 0.06);\n";
            }
            std::string ground = dark ? l.ground_dark : l.ground;
            if (!ground.empty()) out += "  --uione-code-ground: " + ground + ";\n";
            return out;
        }

    } // namespace detail

    // The theme of that name, with what it's from, from the project's files or uione's
    // own library; nothing when there's none.
    inline std::optional<look> resolve(const std::vector<language::file>& files, const std::string& name) {
        std::map<std::string, const language::theme_declaration*> themes;
        for (const auto& f : files) detail::collect(f.declarations, themes);
        detail::collect(detail::library().declarations, themes);
        std::vector<const language::theme_declaration*> chain;
        std::set<std::string> seen;
        for (std::string at = name; !at.empty();) {
            auto it = themes.find(at);
            if (it == themes.end() || !seen.insert(at).second) return std::nullopt;
            chain.push_back(it->second);
            at = it->second->from;
        }
        look l;
        // The first of uione's own it comes to, like papercolor, which is from harbor.
        std::map<std::string, const language::theme_declaration*> own;
        detail::collect(detail::library().declarations, own);
        for (const auto* t : chain) {
            if (own.contains(t->name) && own.at(t->name) == t) {
                l.base = t->name;
                break;
            }
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const auto& t = **it;
            for (const auto& c : t.colors) l.colors[c.role] = {c.light, c.dark.empty() ? c.light : c.dark};
            if (t.text) l.text = *t.text;
            if (t.heading) l.heading = *t.heading;
            if (t.code) l.code = *t.code;
            if (t.ground) l.ground = t.ground->light, l.ground_dark = t.ground->dark.empty() ? t.ground->light : t.ground->dark;
            if (t.corners) l.corners = *t.corners;
            if (t.depth) l.depth = *t.depth;
        }
        for (const auto& role : {"page", "surface", "sunken", "ink", "muted", "line", "accent", "danger", "success", "warning"}) {
            if (!l.colors.contains(role)) return std::nullopt;
        }
        return l;
    }

    // The variables, light; dark as the system is unless the reader picked; and dark
    // when they did.
    inline std::string css(const look& l) {
        std::string shape = "  --radius-box: " + std::to_string(l.corners) + "px;\n  --radius-control: " + std::to_string(l.corners > 2 ? l.corners - 2 : l.corners) +
                            "px;\n  --font-sans: " + detail::stack(l.text, false) + ";\n  --font-heading: " +
                            detail::stack(l.heading.empty() ? l.text : l.heading, false) + ";\n  --font-mono: " + detail::stack(l.code, true) + ";\n";
        return "/* Generated by one from the site's theme. Do not edit. */\n"
               ":root {\n" + shape + detail::side(l, false) + "}\n"
               "@media (prefers-color-scheme: dark) {\n  :root:not([data-theme=\"light\"]) {\n" + detail::side(l, true) + "  }\n}\n"
               ":root[data-theme=\"dark\"] {\n" + detail::side(l, true) + "}\n";
    }

    // The fonts the component set doesn't bring, from Google Fonts; empty when it brings them all.
    inline std::string fonts_link(const look& l) {
        std::set<std::string> families{l.text, l.heading.empty() ? l.text : l.heading, l.code};
        std::string query;
        for (const auto& f : families) {
            if (f == "IBM Plex Sans" || f == "IBM Plex Mono") continue;
            std::string name;
            for (char c : f) name += c == ' ' ? '+' : c;
            query += "&family=" + name + ":wght@400;500;600;700";
        }
        if (query.empty()) return "";
        return "https://fonts.googleapis.com/css2?" + query.substr(1) + "&display=swap";
    }

} // namespace one::generators::theme
