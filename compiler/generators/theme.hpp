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

        static constexpr const char* roles[] = {"page", "surface", "sunken", "ink", "muted", "line", "accent", "danger", "success", "warning"};

        // A color moved, by lightness alone, until it reads at ratio on each ground.
        inline std::string reads(std::string color, const std::vector<std::string>& grounds, double ratio) {
            for (int round = 0; round < 3; ++round) {
                const std::string* worst = &grounds.front();
                for (const auto& g : grounds) {
                    if (language::colors::contrast(color, g) < language::colors::contrast(color, *worst)) worst = &g;
                }
                if (language::colors::contrast(color, *worst) >= ratio) return color;
                std::string moved = language::colors::nearest(color, *worst, ratio);
                if (moved.empty()) return language::colors::dark_ground(*worst) ? "#ffffff" : "#000000";
                color = moved;
            }
            return color;
        }

        // The theme's colors in one mode, as it says them and with more contrast: text
        // black or white, quieter text and the rest at 7:1 on the page and on panels,
        // as WCAG's AAA asks, and lines at 3:1.
        inline std::string mode(const look& l, bool dark) {
            auto c = [&](const std::string& role) { return dark ? l.colors.at(role).second : l.colors.at(role).first; };
            std::string out;
            auto put = [&](const std::string& name, const std::string& value) { out += "  --" + name + ": " + value + ";\n"; };
            std::map<std::string, std::string> more;
            for (const char* role : roles) more[role] = c(role);
            std::vector<std::string> grounds{c("page"), c("surface")};
            for (const char* role : {"muted", "accent", "danger", "success", "warning"}) more[role] = reads(c(role), grounds, 7);
            // Text itself as far from the page as it goes.
            more["ink"] = language::colors::dark_ground(c("page")) ? "#ffffff" : "#000000";
            more["line"] = reads(c("line"), {c("page")}, 3);
            for (const char* role : roles) {
                put(std::string("std-") + role, c(role));
                put(std::string("more-") + role, more[role]);
            }
            put("std-accent-ink", language::colors::on(c("accent"), c("page")));
            put("more-accent-ink", language::colors::on(more["accent"], c("page")));
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

        // The colors the component set reads, from one set of the theme's: as it says
        // them, or with more contrast.
        inline std::string chosen(const std::string& set) {
            std::string out;
            for (const char* role : roles) out += "  --color-" + std::string(role) + ": var(--" + set + "-" + role + ");\n";
            out += "  --color-accent-ink: var(--" + set + "-accent-ink);\n";
            return out;
        }

        // What's worked out from the roles, whichever set they're from.
        inline std::string worked_out() {
            std::string out;
            auto put = [&](const std::string& name, const std::string& value) { out += "  --color-" + name + ": " + value + ";\n"; };
            put("accent-hover", "color-mix(in oklch, var(--color-accent) 85%, var(--color-ink))");
            put("accent-soft", "color-mix(in oklch, var(--color-accent) 14%, var(--color-surface))");
            // The edge of a field stands out from the page by 3:1.
            put("control-line", "color-mix(in oklch, var(--color-ink) 45%, var(--color-page))");
            put("grid", "color-mix(in srgb, var(--color-accent) 7%, transparent)");
            // How urgent something is, as Trac colored it: the most urgent the danger's,
            // then the warning's, then plain, then what can wait in the theme's own.
            for (const auto& [n, from] : std::map<std::string, std::string>{{"1", "danger"}, {"2", "warning"}, {"4", "success"}, {"5", "accent"}}) {
                put("tone-" + n, "color-mix(in oklch, var(--color-" + from + ") 16%, var(--color-surface))");
                put("tone-" + n + "-edge", "color-mix(in oklch, var(--color-" + from + ") 55%, var(--color-surface))");
            }
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
        // Each mode's sets: light; dark as the system is unless the reader picked; and
        // dark when they did.
        // Then the set drawn with: as the theme says, or with more contrast, when the
        // system asks for it unless the reader said otherwise, or when they did.
        return "/* Generated by one from the site's theme. Do not edit. */\n"
               ":root {\n" + shape + detail::mode(l, false) + "}\n"
               "@media (prefers-color-scheme: dark) {\n  :root:not([data-theme=\"light\"]) {\n" + detail::mode(l, true) + "  }\n}\n"
               ":root[data-theme=\"dark\"] {\n" + detail::mode(l, true) + "}\n"
               ":root {\n" + detail::chosen("std") + detail::worked_out() + "}\n"
               "@media (prefers-contrast: more) {\n  :root:not([data-contrast=\"standard\"]) {\n" + detail::chosen("more") + "  }\n}\n"
               ":root[data-contrast=\"more\"] {\n" + detail::chosen("more") + "}\n";
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
