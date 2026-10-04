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
#include <string>
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

    struct checked {
        language::diagnostics problems;
        std::size_t files = 0;
        std::optional<driver::outline> project;  // when a project block parsed
    };

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

    // Each root is a project, checked on its own.
    inline checked check(const std::vector<std::string>& roots) {
        checked out;
        for (const auto& root : roots) {
            std::vector<language::file> files;
            out.files += check_project(root, out.problems, &files);
            if (!out.project) out.project = outline_of(files);
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
