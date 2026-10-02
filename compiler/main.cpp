// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one, the uione compiler.
//
//   one check <path>...                  check every .one file under each path, treating
//                                        each path as one project, and report mistakes
//   one build <project> [--out <folder>]  check a project, then write its web app,
//                                        backend and Firestore rules to <folder>, or
//                                        <project>/build

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "generators/project.hpp"
#include "language/checker.hpp"
#include "language/diagnostics.hpp"
#include "language/parser.hpp"
#include "platform/files.hpp"

namespace {

    constexpr std::string_view version = "0.0.1";

    int usage() {
        std::cerr << "usage: one check <path>...\n"
                     "       one build <project> [--out <folder>]\n"
                     "       one show <file>:<line>[-<line>]\n"
                     "       one --version\n";
        return 2;
    }

    // Reads one project's files and checks them together. The checker only runs when
    // everything parsed, since a half-read file would only cause follow-on errors.
    std::size_t check_project(const std::string& root, one::language::diagnostics& found,
                              std::vector<one::language::file>* kept = nullptr) {
        auto paths = one::platform::find_one_files({root});
        if (paths.empty()) {
            found.push_back({root, {}, "there are no .one files here"});
            return 0;
        }
        std::size_t errors_before = found.size();
        std::vector<one::language::file> files;
        for (const auto& path : paths) {
            auto source = one::platform::read_file(path);
            if (!source) {
                found.push_back({path, {}, "can't read this file"});
                continue;
            }
            files.push_back(one::language::parse(path, *source, found));
        }
        if (found.size() == errors_before) one::language::check(files, found);
        if (kept) *kept = std::move(files);
        return paths.size();
    }

    int check(const std::vector<std::string>& roots) {
        one::language::diagnostics found;
        std::size_t files = 0;
        for (const auto& root : roots) files += check_project(root, found);
        for (const auto& d : found) std::cerr << one::language::format(d) << "\n";
        if (!found.empty()) {
            std::cerr << found.size() << (found.size() == 1 ? " error" : " errors") << " in "
                      << files << (files == 1 ? " file\n" : " files\n");
            return 1;
        }
        std::cout << files << (files == 1 ? " file" : " files") << ", no errors\n";
        return 0;
    }

    // Checks a project, and only when it's clean, writes its web app.
    int build(const std::string& project, std::string out) {
        if (out.empty()) out = project + "/build";
        one::language::diagnostics found;
        std::vector<one::language::file> files;
        std::size_t count = check_project(project, found, &files);
        for (const auto& d : found) std::cerr << one::language::format(d) << "\n";
        if (!found.empty() || count == 0) {
            std::cerr << "one: nothing was built, because the project has errors\n";
            return 1;
        }
        auto generated = one::generators::generate_project(files, project, out);
        for (const auto& d : generated.errors) std::cerr << one::language::format(d) << "\n";
        if (!generated.errors.empty()) {
            std::cerr << "one: nothing was built, because the backend can't be generated yet\n";
            return 1;
        }
        if (!generated.note.empty()) std::cerr << "one: " << generated.note << "\n";
        std::size_t count_written = 0;
        for (const auto& file : generated.files) {
            std::string path = out + "/" + file.path;
            if (!one::platform::write_file(path, file.content) || (file.executable && !one::platform::make_executable(path))) {
                std::cerr << "one: can't write " << path << "\n";
                return 1;
            }
            ++count_written;
        }
        std::cout << "wrote " << count_written << " files to " << out << "\n";
        return 0;
    }

    // Prints the generated code that came from a line, or a range of lines, of a .one
    // file, in every output: what each part of the language turns into, the way an
    // assembly view shows a block of C++.
    int show(const std::string& where) {
        namespace fs = std::filesystem;
        auto colon = where.rfind(':');
        if (colon == std::string::npos) return usage();
        std::string path = where.substr(0, colon), range = where.substr(colon + 1);
        int from = 0, to = 0;
        try {
            auto dash = range.find('-');
            from = std::stoi(range.substr(0, dash));
            to = dash == std::string::npos ? from : std::stoi(range.substr(dash + 1));
        } catch (const std::exception&) {
            return usage();
        }
        if (from < 1 || to < from) return usage();
        std::string project = fs::path(path).parent_path().string();
        if (project.empty()) project = ".";
        one::language::diagnostics found;
        std::vector<one::language::file> files;
        check_project(project, found, &files);
        for (const auto& d : found) std::cerr << one::language::format(d) << "\n";
        if (!found.empty()) return 1;
        auto generated = one::generators::generate_project(files, project, project + "/build");
        for (const auto& d : generated.errors) std::cerr << one::language::format(d) << "\n";
        if (!generated.errors.empty()) return 1;

        auto wanted = fs::weakly_canonical(path);
        std::size_t shown = 0;
        for (const auto& file : generated.files) {
            std::vector<std::string> lines;
            std::size_t at = 0;
            for (std::size_t n = 0; n < file.sources.size() && at < file.content.size(); ++n) {
                std::size_t end = file.content.find('\n', at);
                const auto& source = file.sources[n];
                bool blank = at == end;
                if (!blank && source && !source->fixed() && source->line >= from && source->line <= to &&
                    fs::weakly_canonical(source->path) == wanted) {
                    std::string number = std::to_string(n + 1);
                    lines.push_back(std::string(5 - std::min<std::size_t>(5, number.size()), ' ') + number + "  " +
                                    file.content.substr(at, end - at));
                }
                at = end + 1;
            }
            if (lines.empty()) continue;
            std::cout << (shown++ ? "\n" : "") << file.path << "\n";
            for (const auto& l : lines) std::cout << l << "\n";
        }
        if (shown == 0) std::cout << "nothing is generated from " << where << "\n";
        return 0;
    }

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() == 1 && args[0] == "--version") {
        std::cout << "one " << version << "\n";
        return 0;
    }
    if (args.size() >= 2 && args[0] == "check") {
        return check({args.begin() + 1, args.end()});
    }
    if (args.size() == 2 && args[0] == "build") return build(args[1], "");
    if (args.size() == 4 && args[0] == "build" && args[2] == "--out") return build(args[1], args[3]);
    if (args.size() == 2 && args[0] == "show") return show(args[1]);
    return usage();
}
