// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What the compiler does, apart from where its answers go: check, build and show
// return what they found, and the command line and the WebAssembly build each
// report it their own way.

#pragma once

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <regex>
#include <tuple>
#include <type_traits>
#include <string>
#include <variant>
#include <vector>

#include "generators/project.hpp"
#include "language/checker.hpp"
#include "language/diagnostics.hpp"
#include "language/parser.hpp"
#include "platform/files.hpp"

namespace one::driver {

    // Where a project block's parts are, for an editor that changes them: each
    // setting's line, and each environment's first and last, by the block's file.
    struct outline {
        struct setting {
            std::string key;
            std::string value;
            int line = 0;
        };
        struct environment {
            std::string name;
            int line = 0;
            int end = 0;  // the line of its closing brace
            std::vector<setting> settings;
        };
        std::string path;
        std::string name;
        int line = 0;
        int end = 0;
        std::vector<setting> settings;
        std::vector<environment> environments;
    };

    // A screen's parts, for an editor that lays it out: its layout, and each item
    // where it starts, a region holding its own. An item is named by its kind, like
    // table or button, what it shows or runs, and what it says, when it says
    // something.
    //
    // A table says more: its columns, and how it's divided, searched, sorted and
    // paged, each with its line, or 0 when it isn't; and what its rows hold, which is
    // what it can show, the ones with choices marked, since those are what it can be
    // divided by.
    struct outlined_column {
        std::string value;  // implemented_by.name
        std::string label;  // "Implementing", when it has one
        bool labeled = false;
        int line = 0;
        std::string when;   // person != me, as written, on a row's button
    };

    struct outlined_row {
        std::string name;
        bool choices = false;
    };

    struct outlined_table {
        std::vector<outlined_column> columns;
        std::vector<outlined_row> rows;
        std::string by;
        std::vector<std::string> search;
        std::string sort;  // -number, for the largest first
        int page = 0;
        std::string link;
    };

    struct outlined_item {
        std::string kind;     // table, form, button, text, region, ...
        std::string subject;  // projects::issue_page.comments, issue::close, main
        std::string label;    // "Close issue", when it has one
        int line = 0;
        std::vector<outlined_item> items;  // a region's, a section's or a menu's
        std::optional<outlined_table> table;
    };

    struct outlined_screen {
        std::string path;
        std::string title;
        std::string route;
        int line = 0;
        std::string layout;   // as written; empty when the project's is used
        int layout_line = 0;  // where it's written, when it is
        std::vector<outlined_item> items;
    };

    struct checked {
        language::diagnostics problems;
        std::size_t files = 0;
        std::optional<driver::outline> project;  // when a project block parsed
        std::vector<outlined_screen> screens;    // when everything parsed
    };

    namespace outline_detail {

        // The views and entities declared, by name and by namespace::name, for a
        // table to find its rows in.
        struct declared {
            std::map<std::string, const language::view_declaration*> views;
            std::map<std::string, const language::entity_declaration*> entities;
            std::string ns;  // the namespace being outlined

            template <typename T>
            const T* find(const std::map<std::string, const T*>& in, const std::string& name) const {
                if (!ns.empty()) {
                    if (auto at = in.find(ns + "::" + name); at != in.end()) return at->second;
                }
                auto at = in.find(name);
                return at == in.end() ? nullptr : at->second;
            }
        };

        inline void collect(const std::vector<language::declaration>& declarations, const std::string& ns, declared& out) {
            for (const auto& d : declarations) {
                std::string prefix = ns.empty() ? "" : ns + "::";
                if (auto* n = std::get_if<language::namespace_declaration>(&d.node)) collect(n->declarations, prefix + n->name, out);
                if (auto* v = std::get_if<language::view_declaration>(&d.node)) out.views[prefix + v->name] = v;
                if (auto* e = std::get_if<language::entity_declaration>(&d.node)) out.entities[prefix + e->name] = e;
            }
        }

        inline std::string written(const language::expression& e) {
            if (auto* name = std::get_if<language::name_expression>(&e.node)) return name->name.text();
            if (auto* member = std::get_if<language::member_expression>(&e.node)) return written(*member->object) + "." + member->member;
            return "";
        }

        inline outlined_table table_of(const language::table_item& t, const declared& known) {
            outlined_table out;
            for (const auto& c : t.columns) out.columns.push_back({written(*c.value), c.label.value_or(""), c.label.has_value(), c.where.line, c.when_written});
            out.by = t.by.value_or("");
            out.search = t.search;
            if (t.sort) out.sort = (t.sort_descending ? "-" : "") + *t.sort;
            out.page = t.page.value_or(0);
            out.link = t.link.value_or("");
            const language::view_declaration* view = known.find(known.views, t.view.text());
            if (!view) return out;
            for (const auto& each : view->each) {
                if (each.name != t.list) continue;
                const language::entity_declaration* listed = nullptr;
                if (each.source && !each.changes) {
                    if (auto* name = std::get_if<language::name_expression>(&each.source->node)) listed = known.find(known.entities, name->name.text());
                }
                for (const auto& row : each.rows) {
                    std::string name = row.name ? *row.name : written(*row.value);
                    if (name.empty()) continue;
                    bool choices = false;
                    if (listed && !row.name) {
                        for (const auto& f : listed->fields) choices = choices || (f.name == name && !f.choices.empty());
                    }
                    out.rows.push_back({name, choices});
                }
            }
            return out;
        }

        inline std::string named(const language::qualified_name& view, const std::optional<std::string>& list) {
            return list ? view.text() + "." + *list : view.text();
        }

        inline outlined_item item_of(const language::screen_item& i, const declared& known) {
            outlined_item out;
            out.line = i.where.line;
            std::visit(
                [&](const auto& n) {
                    using T = std::decay_t<decltype(n)>;
                    if constexpr (std::is_same_v<T, language::content_block>) {
                        using kind = language::content_block::kind;
                        out.kind = n.type == kind::region ? "region" : n.type == kind::menu ? "menu" : n.type == kind::hero ? "hero" : "section";
                        out.subject = n.type == kind::region ? n.title : "";
                        if (n.type != kind::region) out.label = n.title;
                        for (const auto& inner : n.items) out.items.push_back(item_of(inner, known));
                    } else if constexpr (std::is_same_v<T, language::content_text>) {
                        using kind = language::content_text::kind;
                        out.kind = n.type == kind::markdown ? "markdown" : n.type == kind::code ? "code" : "text";
                        out.label = n.value;
                    } else if constexpr (std::is_same_v<T, language::content_link>) {
                        out.kind = "link";
                        out.subject = n.target;
                        out.label = n.label;
                    } else if constexpr (std::is_same_v<T, language::table_item>) {
                        out.kind = "table";
                        out.subject = named(n.view, n.list);
                        out.table = table_of(n, known);
                    } else if constexpr (std::is_same_v<T, language::form_item>) {
                        out.kind = "form";
                        for (const auto& c : n.commands) out.subject += (out.subject.empty() ? "" : " ") + c.text();
                        out.label = n.submit.value_or("");
                    } else if constexpr (std::is_same_v<T, language::confirm_item>) {
                        out.kind = "confirm";
                        out.subject = n.command.text();
                        out.label = n.message;
                    } else if constexpr (std::is_same_v<T, language::button_item>) {
                        out.kind = "button";
                        out.subject = n.command.text();
                        out.label = n.label.value_or("");
                    } else if constexpr (std::is_same_v<T, language::component_item>) {
                        out.kind = "component";
                        out.subject = n.name;
                    } else if constexpr (std::is_same_v<T, language::thread_item>) {
                        out.kind = "thread";
                        out.subject = n.view.text() + "." + n.list;
                    } else if constexpr (std::is_same_v<T, language::timeline_item>) {
                        out.kind = "timeline";
                        out.subject = n.view.text() + "." + n.list;
                    } else if constexpr (std::is_same_v<T, language::copy_item>) {
                        out.kind = "copy";
                        out.subject = n.view.text();
                        out.label = n.label.value_or("");
                    } else if constexpr (std::is_same_v<T, language::details_item>) {
                        out.kind = "details";
                        out.subject = n.view.text();
                    }
                },
                i.node);
            return out;
        }

        inline void screens_in(const std::string& path, const std::vector<language::declaration>& declarations,
                               declared& known, std::vector<outlined_screen>& out) {
            for (const auto& d : declarations) {
                if (auto* n = std::get_if<language::namespace_declaration>(&d.node)) {
                    std::string outer = known.ns;
                    known.ns = outer.empty() ? n->name : outer + "::" + n->name;
                    screens_in(path, n->declarations, known, out);
                    known.ns = outer;
                }
                auto* s = std::get_if<language::screen_declaration>(&d.node);
                if (!s) continue;
                outlined_screen screen{path, s->title, s->route, d.where.line, s->layout.value_or(""),
                                       s->layout ? s->layout_where.line : 0, {}};
                for (const auto& i : s->items) screen.items.push_back(item_of(i, known));
                out.push_back(std::move(screen));
            }
        }

    } // namespace outline_detail

    // Every screen in the files, in the order they're written.
    inline std::vector<outlined_screen> screens_of(const std::vector<language::file>& files) {
        std::vector<outlined_screen> out;
        outline_detail::declared known;
        for (const auto& f : files) outline_detail::collect(f.declarations, "", known);
        for (const auto& f : files) outline_detail::screens_in(f.path, f.declarations, known, out);
        return out;
    }

    inline std::optional<outline> outline_of(const std::vector<language::file>& files) {
        for (const auto& f : files) {
            for (const auto& d : f.declarations) {
                auto* p = std::get_if<language::project_declaration>(&d.node);
                if (!p) continue;
                auto settings = [](const std::vector<language::setting>& from) {
                    std::vector<outline::setting> out;
                    for (const auto& s : from) out.push_back({s.key, s.value, s.where.line});
                    return out;
                };
                outline o{f.path, p->name, d.where.line, p->end.line, settings(p->settings), {}};
                for (const auto& e : p->environments) o.environments.push_back({e.name, e.where.line, e.end.line, settings(e.settings)});
                return o;
            }
        }
        return std::nullopt;
    }

    // A project's .one files, by path, as text.
    using sources = std::map<std::string, std::string>;

    // Checks a project's files as given, which may be edits not yet written, all
    // together. The checker only runs when everything parsed, since a half-read file
    // would only cause follow-on errors.
    inline void check_sources(const sources& given, language::diagnostics& found, std::vector<language::file>* kept = nullptr) {
        std::size_t errors_before = found.size();
        std::vector<language::file> files;
        for (const auto& [path, source] : given) files.push_back(language::parse(path, source, found));
        if (found.size() == errors_before) language::check(files, found);
        if (kept) *kept = std::move(files);
    }

    // Reads a project's .one files, saying which can't be read.
    inline sources read_sources(const std::string& root, language::diagnostics& found) {
        sources read;
        auto paths = platform::find_one_files({root});
        if (paths.empty()) found.push_back({root, {}, "there are no .one files here"});
        for (const auto& path : paths) {
            if (auto source = platform::read_file(path)) read[path] = *source;
            else found.push_back({path, {}, "can't read this file"});
        }
        return read;
    }

    // Reads one project's files and checks them together.
    inline std::size_t check_project(const std::string& root, language::diagnostics& found,
                                     std::vector<language::file>* kept = nullptr) {
        std::size_t errors_before = found.size();
        auto read = read_sources(root, found);
        if (read.empty()) return 0;
        if (found.size() == errors_before) check_sources(read, found, kept);
        return platform::find_one_files({root}).size();
    }

    // Applies fixes to a project's files: each once, the later ones in a file first,
    // so the earlier ones' places stay where they were.
    inline std::size_t apply_fixes(sources& files, const language::diagnostics& found) {
        std::map<std::string, std::vector<const language::diagnostic*>> by_file;
        for (const auto& d : found) {
            if (d.fix && files.contains(d.path)) by_file[d.path].push_back(&d);
        }
        std::size_t applied = 0;
        for (auto& [path, list] : by_file) {
            std::sort(list.begin(), list.end(), [](auto* a, auto* b) {
                return std::tie(b->fix->where.line, b->fix->where.column) < std::tie(a->fix->where.line, a->fix->where.column);
            });
            std::string& text = files[path];
            std::pair<std::size_t, std::size_t> last{0, 0};
            for (auto* d : list) {
                std::pair<std::size_t, std::size_t> at{d->fix->where.line, d->fix->where.column};
                if (at == last) continue;
                last = at;
                // From line and column, both counted from 1, to a place in the text.
                std::size_t offset = 0;
                for (std::size_t line = 1; line < at.first && offset != std::string::npos; ++line) {
                    offset = text.find('\n', offset);
                    if (offset != std::string::npos) ++offset;
                }
                if (offset == std::string::npos) continue;
                offset += at.second - 1;
                if (offset + d->fix->length > text.size()) continue;
                text.replace(offset, d->fix->length, d->fix->text);
                ++applied;
            }
        }
        return applied;
    }

    // Says in a project's block which compiler it's for, `one "0.4.0"`: changing the
    // line when there is one, and adding it under the block's first line when there
    // isn't, lined up with the settings beside it. False when there's no project block.
    inline bool record_version(sources& files, std::string_view version) {
        static const std::regex block(R"re((^|\n)[ \t]*project[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*\{[^\n]*\n)re");
        static const std::regex existing(R"re((^|\n)([ \t]*one[ \t]+)"[^"\n]*")re");
        static const std::regex setting(R"re(^([ \t]+)([a-z_]+)([ \t]+)\S)re");
        for (auto& [path, text] : files) {
            std::smatch found;
            if (!std::regex_search(text, found, block)) continue;
            std::size_t start = static_cast<std::size_t>(found.position(0) + found.length(0));
            std::size_t end = text.find("\n}", start);
            std::string inside = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
            std::smatch line;
            if (std::regex_search(inside, line, existing)) {
                std::size_t at = start + static_cast<std::size_t>(line.position(2) + line.length(2));
                std::size_t close = text.find('"', at + 1);
                text.replace(at, close + 1 - at, "\"" + std::string(version) + "\"");
                return true;
            }
            std::string first = inside.substr(0, inside.find('\n'));
            std::smatch beside;
            std::string indent = "\t", gap = "  ";
            if (std::regex_search(first, beside, setting)) {
                indent = beside[1];
                std::size_t width = beside[2].length() + beside[3].length();
                gap = std::string(width > 3 ? width - 3 : 2, ' ');
            }
            text.insert(start, indent + "one" + gap + "\"" + std::string(version) + "\"\n");
            return true;
        }
        return false;
    }

    // A project block whose settings' values were lined up stays lined up when an
    // upgrade renames a setting, like signin to authentication: its values move out
    // to the column the longest name needs, and no further.
    inline std::string align_settings(const std::string& before, const std::string& after) {
        static const std::regex opens(R"(^[ \t]*project[ \t]+\w+[ \t]*\{[ \t]*$)");
        static const std::regex setting(R"(^([ \t]+)([a-z_]+)([ \t]+)([^ \t{][^{]*)$)");
        auto lines_of = [](const std::string& text) {
            std::vector<std::string> lines;
            std::size_t at = 0;
            for (std::size_t end; (end = text.find('\n', at)) != std::string::npos; at = end + 1) lines.push_back(text.substr(at, end - at));
            lines.push_back(text.substr(at));
            return lines;
        };
        // The settings directly in the project block, by line, as indent, name and value.
        struct line_setting {
            std::size_t line;
            std::string indent, key, value;
            std::size_t column;
        };
        auto settings_of = [&](const std::vector<std::string>& lines) {
            std::vector<line_setting> found;
            std::size_t i = 0;
            while (i < lines.size() && !std::regex_match(lines[i], opens)) ++i;
            std::string indent;
            for (++i; i < lines.size() && !lines[i].starts_with("}"); ++i) {
                std::smatch m;
                if (!std::regex_match(lines[i], m, setting)) continue;
                if (indent.empty()) indent = m[1];
                if (m[1] != indent) continue;  // inside an environment
                found.push_back({i, m[1], m[2], m[4], static_cast<std::size_t>(m[1].length() + m[2].length() + m[3].length())});
            }
            return found;
        };
        auto lines = lines_of(after);
        auto was = settings_of(lines_of(before));
        auto now = settings_of(lines);
        if (was.size() < 2 || was.size() != now.size()) return after;
        std::size_t column = was[0].column;
        for (const auto& s : was) {
            if (s.column != column) return after;  // not lined up to begin with
        }
        std::size_t needed = column;
        for (const auto& s : now) needed = std::max(needed, s.indent.size() + s.key.size() + 2);
        if (needed == column) return after;
        for (const auto& s : now) lines[s.line] = s.indent + s.key + std::string(needed - s.indent.size() - s.key.size(), ' ') + s.value;
        std::string out;
        for (std::size_t i = 0; i < lines.size(); ++i) out += (i ? "\n" : "") + lines[i];
        return out;
    }

    // An entity's choices written as an enum by an upgrade start in the column the
    // block's rules do, as they'd have been written, so the line still reads as name,
    // type and rules:
    //     person   user     required  key
    //     role     enum     maintainer | reporter = role::reporter
    // Only lines that became enums are moved; the rules' column is the one most fields
    // whose type starts where the enum's does use.
    inline std::string align_choices(const std::string& before, const std::string& after) {
        auto split = [](const std::string& text) {
            std::vector<std::string> lines;
            std::size_t at = 0;
            for (std::size_t end; (end = text.find('\n', at)) != std::string::npos; at = end + 1) lines.push_back(text.substr(at, end - at));
            lines.push_back(text.substr(at));
            return lines;
        };
        auto was = split(before);
        auto lines = split(after);
        if (was.size() != lines.size()) return after;  // fixes never add lines
        static const std::regex enum_field(R"(^(\s+\w+\s+)enum\s+(.*)$)");
        static const std::regex field(R"(^(\s+\w+\s+)(list of \w+|[\w:]+)(\s+)\S.*$)");
        static const std::regex opens(R"(^\s*entity\s+\w+.*\{\s*$)");
        static const std::regex closes(R"(^\s*\}.*$)");
        for (std::size_t i = 0; i < lines.size(); ++i) {
            std::smatch m;
            if (!std::regex_match(lines[i], m, enum_field) || std::regex_search(was[i], std::regex(R"(\benum\b)"))) continue;
            std::string head = m[1], rest = m[2];
            std::size_t first = i, last = i;
            while (first > 0 && !std::regex_match(lines[first], opens)) --first;
            while (last + 1 < lines.size() && !std::regex_match(lines[last], closes)) ++last;
            std::map<std::size_t, int> rules;  // where rules start, among fields typed in the enum's column
            for (std::size_t k = first + 1; k < last; ++k) {
                std::smatch f;
                if (k == i || !std::regex_match(lines[k], f, field) || f[2] == "enum" || static_cast<std::size_t>(f[1].length()) != head.size()) continue;
                ++rules[head.size() + static_cast<std::size_t>(f[2].length() + f[3].length())];
            }
            std::size_t column = head.size() + 6;  // "enum" and two spaces, with nothing to line up with
            int most = 0;
            for (const auto& [at, count] : rules) {
                if (count > most) most = count, column = std::max(at, head.size() + 6);
            }
            lines[i] = head + "enum" + std::string(column - head.size() - 4, ' ') + rest;
        }
        std::string out;
        for (std::size_t i = 0; i < lines.size(); ++i) out += (i ? "\n" : "") + lines[i];
        return out;
    }

    struct upgraded {
        sources changed;                 // the files it changed, as they are now
        language::diagnostics problems;  // what's left that has no fix; nothing is changed while there's any
        std::size_t fixes = 0;
        bool recorded = false;           // whether the project block now names this compiler
    };

    // Brings a project to this compiler: applies the fixes its mistakes come with, and
    // checks again, until it's clean, then records this compiler's version in its
    // project block. It changes nothing unless it ends clean.
    inline upgraded upgrade(const std::string& root, std::string_view version) {
        upgraded out;
        auto files = read_sources(root, out.problems);
        if (!out.problems.empty()) return out;
        auto original = files;
        for (int round = 0; round < 100; ++round) {
            language::diagnostics found;
            check_sources(files, found);
            std::size_t applied = apply_fixes(files, found);
            out.fixes += applied;
            if (applied == 0) {
                out.problems = std::move(found);
                break;
            }
        }
        if (!out.problems.empty()) return out;
        for (auto& [path, text] : files) text = align_settings(original[path], text);
        for (auto& [path, text] : files) text = align_choices(original[path], text);
        out.recorded = record_version(files, version);
        language::diagnostics after;
        check_sources(files, after);
        if (!after.empty()) {
            out.problems = std::move(after);
            out.recorded = false;
            return out;
        }
        for (const auto& [path, text] : files) {
            if (original[path] != text) out.changed[path] = text;
        }
        return out;
    }

    // What the name at a place in a project's file means, for an editor's hover and
    // its going to a definition: said in words, and where it's declared, or the
    // reference's section for one of the language's own. Nothing, when no name is
    // there, or the project doesn't parse.
    struct definition {
        bool found = false;
        std::string says;
        std::string path;     // where it's declared; empty for the language's own
        int line = 0;
        int column = 0;
        std::string section;  // the reference's section, like built-in-values
        int from = 0;         // the column the name starts at, and the one after it
        int to = 0;
    };

    inline definition define(const sources& given, const std::string& path, int line, int column) {
        language::diagnostics found;
        std::vector<language::file> files;
        for (const auto& [file, source] : given) files.push_back(language::parse(file, source, found));
        if (!found.empty()) return {};
        language::meanings meant;
        language::check(files, found, meant);
        const language::meaning* best = nullptr;
        for (const auto& m : meant) {
            int start = m.where.column, end = m.where.column + static_cast<int>(m.length);
            if (m.path != path || m.where.line != line || column < start || column >= end) continue;
            if (!best || m.length < best->length) best = &m;
        }
        if (!best) return {};
        return {true, best->says, best->to_path, best->to.line, best->to.column, best->section, best->where.column,
                best->where.column + static_cast<int>(best->length)};
    }

    inline definition define(const std::string& root, const std::string& path, int line, int column) {
        language::diagnostics found;
        auto read = read_sources(root, found);
        if (!found.empty()) return {};
        return define(read, path, line, column);
    }

    // Each root is a project, checked on its own.
    inline checked check(const std::vector<std::string>& roots) {
        checked out;
        for (const auto& root : roots) {
            std::vector<language::file> files;
            out.files += check_project(root, out.problems, &files);
            if (!out.project) out.project = outline_of(files);
            auto screens = screens_of(files);
            out.screens.insert(out.screens.end(), screens.begin(), screens.end());
        }
        return out;
    }

    struct built {
        language::diagnostics problems;  // why nothing was generated, if it wasn't
        std::string refusal;             // the same, in a sentence
        std::vector<generators::output_file> files;
        std::string note;
        std::string environment;         // the one it's built for, when the project has them
    };

    // Chooses the environment a project is built for: the one named, or with none
    // named, the first. Its settings take the place of the shared ones they name, so
    // what's generated is for that one place. Says what's wrong, or "" when nothing is.
    inline std::string choose_environment(std::vector<language::file>& files, const std::string& wanted, std::string& chosen) {
        for (auto& f : files) {
            for (auto& d : f.declarations) {
                auto* p = std::get_if<language::project_declaration>(&d.node);
                if (!p) continue;
                if (p->environments.empty()) {
                    if (!wanted.empty()) return "this project has no environments, so it's built without --for";
                    return "";
                }
                const language::environment_block* environment = &p->environments.front();
                if (!wanted.empty()) {
                    environment = nullptr;
                    std::string names;
                    for (const auto& e : p->environments) {
                        if (e.name == wanted) environment = &e;
                        names += (names.empty() ? "" : ", ") + e.name;
                    }
                    if (!environment) return "there's no environment " + wanted + "; this project has " + names;
                }
                for (const auto& own : environment->settings) {
                    auto shared = std::find_if(p->settings.begin(), p->settings.end(), [&](const auto& s) { return s.key == own.key; });
                    if (shared != p->settings.end()) *shared = own;
                    else p->settings.push_back(own);
                }
                p->environment = environment->name;
                chosen = environment->name;
                return "";
            }
        }
        if (!wanted.empty()) return "this project has no project block, so it has no environments";
        return "";
    }

    // Checks a project, and only when it's clean, generates everything it becomes,
    // for the environment named, or the first. Nothing is written; that's up to the
    // caller.
    inline built build(const std::string& project, const std::string& out, const std::string& environment = "") {
        built result;
        std::vector<language::file> files;
        std::size_t count = check_project(project, result.problems, &files);
        if (!result.problems.empty() || count == 0) {
            result.refusal = "nothing was built, because the project has errors";
            return result;
        }
        std::string chosen;
        if (auto wrong = choose_environment(files, environment, chosen); !wrong.empty()) {
            result.refusal = wrong;
            return result;
        }
        auto generated = generators::generate_project(files, project, out);
        if (!generated.errors.empty()) {
            result.problems = std::move(generated.errors);
            result.refusal = "nothing was built, because the backend can't be generated yet";
            return result;
        }
        result.files = std::move(generated.files);
        result.note = std::move(generated.note);
        if (!chosen.empty() && environment.empty()) {
            result.note = (result.note.empty() ? "" : result.note + "; ") + "built for environment " + chosen + ", the first; --for names another";
        }
        result.environment = chosen;
        return result;
    }

    struct shown_file {
        std::string path;
        std::vector<std::pair<std::size_t, std::string>> lines;  // numbered from 1
    };

    struct shown {
        language::diagnostics problems;
        bool understood = true;  // false when `where` isn't file:line or file:line-line
        std::vector<shown_file> files;
    };

    // The generated code that came from a line, or a range of lines, of a .one file,
    // in every output: what each part of the language turns into, the way an
    // assembly view shows a block of C++.
    inline shown show(const std::string& where) {
        namespace fs = std::filesystem;
        shown result;
        auto colon = where.rfind(':');
        if (colon == std::string::npos) {
            result.understood = false;
            return result;
        }
        std::string path = where.substr(0, colon), range = where.substr(colon + 1);
        int from = 0, to = 0;
        try {
            auto dash = range.find('-');
            from = std::stoi(range.substr(0, dash));
            to = dash == std::string::npos ? from : std::stoi(range.substr(dash + 1));
        } catch (const std::exception&) {
            result.understood = false;
            return result;
        }
        if (from < 1 || to < from) {
            result.understood = false;
            return result;
        }
        std::string project = fs::path(path).parent_path().string();
        if (project.empty()) project = ".";
        std::vector<language::file> files;
        check_project(project, result.problems, &files);
        if (!result.problems.empty()) return result;
        auto generated = generators::generate_project(files, project, project + "/build");
        if (!generated.errors.empty()) {
            result.problems = std::move(generated.errors);
            return result;
        }

        auto wanted = fs::weakly_canonical(path);
        for (const auto& file : generated.files) {
            shown_file found{file.path, {}};
            std::size_t at = 0;
            for (std::size_t n = 0; n < file.sources.size() && at < file.content.size(); ++n) {
                std::size_t end = file.content.find('\n', at);
                const auto& source = file.sources[n];
                bool blank = at == end;
                if (!blank && source && !source->fixed() && source->line >= from && source->line <= to &&
                    fs::weakly_canonical(source->path) == wanted) {
                    found.lines.emplace_back(n + 1, file.content.substr(at, end - at));
                }
                at = end + 1;
            }
            if (!found.lines.empty()) result.files.push_back(std::move(found));
        }
        return result;
    }

} // namespace one::driver
