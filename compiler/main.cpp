// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one, the uione compiler.
//
//   one check <path>...                  check every .one file under each path, treating
//                                        each path as one project, and report mistakes
//   one build <project> [--out <folder>]  check a project, then write its web app,
//                                        backend and Firestore rules to <folder>, or
//                                        <project>/build
//   one upgrade <project>                bring a project to this compiler: apply the
//                                        fixes its mistakes come with, and record this
//                                        version in its project block
//
// A project whose block names another compiler, one "0.4.0", is handed to that
// version (toolchain.hpp).

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "driver.hpp"
#include "toolchain.hpp"
#include "language/diagnostics.hpp"
#include "platform/files.hpp"
#include "version.hpp"

namespace {

    int usage() {
        std::cerr << "usage: one check <path>...\n"
                     "       one build <project> [--out <folder>]\n"
                     "       one show <file>:<line>[-<line>]\n"
                     "       one upgrade <project>\n"
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

    int upgrade(const std::string& project) {
        auto before = one::toolchain::pinned(project);
        auto done = one::driver::upgrade(project, one::version);
        if (!done.problems.empty()) {
            report(done.problems);
            std::cerr << "one: " << done.problems.size() << (done.problems.size() == 1 ? " mistake has" : " mistakes have")
                      << " no fix to apply, so nothing was changed; fix " << (done.problems.size() == 1 ? "it" : "them")
                      << " and upgrade again\n";
            return 1;
        }
        for (const auto& [path, text] : done.changed) {
            if (!one::platform::write_file(path, text)) {
                std::cerr << "one: can't write " << path << "\n";
                return 1;
            }
            std::cout << "changed " << path << "\n";
        }
        std::cout << done.fixes << (done.fixes == 1 ? " fix" : " fixes") << " applied; ";
        if (done.recorded) {
            std::cout << "the project is for one " << one::version << (before && *before != one::version ? ", from " + *before : "") << "\n";
        } else {
            std::cout << "the project has no project block to record one " << one::version << " in\n";
        }
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
    if (args.size() == 2 && args[0] == "upgrade") return upgrade(args[1]);

    // A project for another compiler is handed to it, before anything is read.
    std::vector<std::string> roots;
    if (!args.empty() && (args[0] == "check" || args[0] == "build")) {
        if (args[0] == "check") roots.assign(args.begin() + 1, args.end());
        else if (args.size() >= 2) roots.push_back(args[1]);
    } else if (args.size() == 2 && args[0] == "show") {
        roots.push_back(std::filesystem::path(args[1].substr(0, args[1].rfind(':'))).parent_path().string());
        if (roots.back().empty()) roots.back() = ".";
    }
    std::string why;
    if (auto version = one::toolchain::wanted(roots, one::version, why)) {
        if (!std::filesystem::exists(one::toolchain::kept(*version))) {
            std::cerr << "one: this project is for one " << *version << "; fetching it\n";
            if (!one::toolchain::fetch(*version, why)) {
                std::cerr << "one: " << why << "\n";
                return 1;
            }
        }
        one::toolchain::hand_over(*version, argv);
        std::cerr << "one: couldn't run one " << *version << " at " << one::toolchain::kept(*version) << "\n";
        return 1;
    }
    if (!why.empty()) {
        std::cerr << "one: " << why << "\n";
        return 2;
    }
    if (args.size() >= 2 && args[0] == "check") {
        return check({args.begin() + 1, args.end()});
    }
    if (args.size() == 2 && args[0] == "build") return build(args[1], "");
    if (args.size() == 4 && args[0] == "build" && args[2] == "--out") return build(args[1], args[3]);
    if (args.size() == 2 && args[0] == "show") return show(args[1]);
    return usage();
}
