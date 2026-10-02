// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one, the uione compiler.
//
//   one check <path>...                  check every .one file under each path, treating
//                                        each path as one project, and report mistakes
//   one build <project> [--out <folder>]  check a project, then write its web app,
//                                        backend and Firestore rules to <folder>, or
//                                        <project>/build

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "driver.hpp"
#include "language/diagnostics.hpp"
#include "platform/files.hpp"
#include "version.hpp"

namespace {

    int usage() {
        std::cerr << "usage: one check <path>...\n"
                     "       one build <project> [--out <folder>]\n"
                     "       one show <file>:<line>[-<line>]\n"
                     "       one --version\n";
        return 2;
    }

    void report(const one::language::diagnostics& problems) {
        for (const auto& d : problems) std::cerr << one::language::format(d) << "\n";
    }

    int check(const std::vector<std::string>& roots) {
        auto checked = one::driver::check(roots);
        report(checked.problems);
        if (!checked.problems.empty()) {
            std::cerr << checked.problems.size() << (checked.problems.size() == 1 ? " error" : " errors") << " in "
                      << checked.files << (checked.files == 1 ? " file\n" : " files\n");
            return 1;
        }
        std::cout << checked.files << (checked.files == 1 ? " file" : " files") << ", no errors\n";
        return 0;
    }

    int build(const std::string& project, std::string out) {
        if (out.empty()) out = project + "/build";
        auto built = one::driver::build(project, out);
        report(built.problems);
        if (!built.refusal.empty()) {
            std::cerr << "one: " << built.refusal << "\n";
            return 1;
        }
        if (!built.note.empty()) std::cerr << "one: " << built.note << "\n";
        for (const auto& file : built.files) {
            std::string path = out + "/" + file.path;
            if (!one::platform::write_file(path, file.content) || (file.executable && !one::platform::make_executable(path))) {
                std::cerr << "one: can't write " << path << "\n";
                return 1;
            }
        }
        std::cout << "wrote " << built.files.size() << " files to " << out << "\n";
        return 0;
    }

    int show(const std::string& where) {
        auto shown = one::driver::show(where);
        if (!shown.understood) return usage();
        report(shown.problems);
        if (!shown.problems.empty()) return 1;
        for (std::size_t i = 0; i < shown.files.size(); ++i) {
            std::cout << (i ? "\n" : "") << shown.files[i].path << "\n";
            for (const auto& [number, text] : shown.files[i].lines) {
                std::string n = std::to_string(number);
                std::cout << std::string(5 - std::min<std::size_t>(5, n.size()), ' ') << n << "  " << text << "\n";
            }
        }
        if (shown.files.empty()) std::cout << "nothing is generated from " << where << "\n";
        return 0;
    }

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() == 1 && args[0] == "--version") {
        std::cout << "one " << one::version << "\n";
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
