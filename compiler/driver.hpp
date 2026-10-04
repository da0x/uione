// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What the compiler does, apart from where its answers go: check, build and show
// return what they found, and the command line and the WebAssembly build each
// report it their own way.

#pragma once

#include <algorithm>
#include <filesystem>
#include <map>
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

    struct checked {
        language::diagnostics problems;
        std::size_t files = 0;
    };

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
        for (const auto& root : roots) out.files += check_project(root, out.problems);
        return out;
    }

    struct built {
        language::diagnostics problems;  // why nothing was generated, if it wasn't
        std::string refusal;             // the same, in a sentence
        std::vector<generators::output_file> files;
        std::string note;
    };

    // Checks a project, and only when it's clean, generates everything it becomes.
    // Nothing is written; that's up to the caller.
    inline built build(const std::string& project, const std::string& out) {
        built result;
        std::vector<language::file> files;
        std::size_t count = check_project(project, result.problems, &files);
        if (!result.problems.empty() || count == 0) {
            result.refusal = "nothing was built, because the project has errors";
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
