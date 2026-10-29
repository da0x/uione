// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one, the uione compiler.
//
//   one check <path>...                  check every .one file under each path, treating
//                                        each path as one project, and report mistakes
//   one build <project> [--out <folder>]  check a project, then write its web app,
//                                        backend and Firestore rules to <folder>, or
//                                        <project>/build
//   one run <project> [--target <target>] build a project, then build and start one of
//                                        its apps: the web app served by Vite, or the
//                                        desktop or terminal app; the first it targets
//                                        unless --target names another
//   one deploy <folder> [--yes]          build every project under a folder, itself
//                                        one or holding several, then deploy each by
//                                        its build/deploy, which asks before it changes
//                                        anything unless --yes
//   one upgrade <project>                bring a project to this compiler: apply the
//                                        fixes its mistakes come with, and record this
//                                        version in its project block
//   one theme [--seed <n>] [--scheme <s>] write a theme made at random from a palette,
//                                        to paste into a .one file
//
// A project whose block names another compiler, one "0.4.0", is handed to that
// version (toolchain.hpp).

#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <optional>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include <one/screens/palette.hpp>

#include "driver.hpp"
#include "toolchain.hpp"
#include "language/diagnostics.hpp"
#include "platform/files.hpp"
#include "version.hpp"

namespace {

    int usage() {
        std::cerr << "usage: one check <path>...\n"
                     "       one build <project> [--out <folder>] [--for <environment>] [--target <target>[,<target>...]]\n"
                     "       one run <project> [--for <environment>] [--target <target>]\n"
                     "       one deploy <folder> [--for <environment>] [--yes]\n"
                     "       one show <file>:<line>[-<line>]\n"
                     "       one upgrade <project>\n"
                     "       one theme [--seed <number>] [--scheme complementary|analogous|triadic|split_complementary|monochrome]\n"
                     "       one --version\n";
        return 2;
    }

    // A theme from a palette, the seed and the scheme picked at random unless they're
    // given; the same seed and scheme are the same theme in every app's themes.
    int theme(const std::vector<std::string>& args) {
        std::random_device device;
        std::uint32_t seed = std::uniform_int_distribution<std::uint32_t>(1, 99999)(device);
        std::string scheme(one::screens::palette::schemes[device() % one::screens::palette::schemes.size()]);
        for (std::size_t i = 1; i < args.size(); i += 2) {
            if (i + 1 >= args.size()) return usage();
            if (args[i] == "--seed") {
                try {
                    std::size_t used = 0;
                    unsigned long n = std::stoul(args[i + 1], &used);
                    if (used != args[i + 1].size() || n > 4294967295ul) return usage();
                    seed = static_cast<std::uint32_t>(n);
                } catch (const std::exception&) {
                    return usage();
                }
            } else if (args[i] == "--scheme" && std::ranges::find(one::screens::palette::schemes, args[i + 1]) != one::screens::palette::schemes.end()) {
                scheme = args[i + 1];
            } else {
                return usage();
            }
        }
        std::cout << one::screens::palette::written(one::screens::palette::generate(seed, scheme));
        return 0;
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

    int build(const std::string& project, std::string out, const std::string& environment, const std::string& targets, one::driver::built* kept = nullptr) {
        if (out.empty()) out = project + "/build";
        // --target web,desktop: fewer of the project's targets, for this run.
        std::optional<std::vector<std::string>> only;
        if (!targets.empty()) {
            only.emplace();
            for (std::size_t from = 0; from <= targets.size();) {
                std::size_t comma = targets.find(',', from);
                std::string one_target = targets.substr(from, comma == std::string::npos ? std::string::npos : comma - from);
                if (!one_target.empty()) only->push_back(one_target);
                if (comma == std::string::npos) break;
                from = comma + 1;
            }
        }
        auto built = one::driver::build(project, out, environment, only);
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
        // A project with no backend, or none any more, has no api/, so a deploy doesn't
        // build one left from before.
        bool backend = std::ranges::any_of(built.files, [](const auto& file) { return file.path.starts_with("api/"); });
        if (!built.files.empty() && !backend && !one::platform::remove_folder(out + "/api")) {
            std::cerr << "one: can't remove " << out << "/api\n";
            return 1;
        }
        std::cout << "wrote " << built.files.size() << " files to " << out << "\n";
        if (kept) *kept = std::move(built);
        return 0;
    }

    // Whether a program is on the PATH.
    bool installed(const std::string& program) {
        const char* path = std::getenv("PATH");
        for (std::string_view left = path ? path : ""; !left.empty();) {
            std::size_t colon = std::min(left.find(':'), left.size());
            std::filesystem::path folder(left.substr(0, colon));
            if (!folder.empty() && access((folder / program).c_str(), X_OK) == 0) return true;
            left.remove_prefix(std::min(colon + 1, left.size()));
        }
        return false;
    }

    // A step's command, as it would be typed, said before it runs.
    void say(const one::driver::step& s) {
        std::cerr << "one:";
        if (s.folder != ".") std::cerr << " (in " << s.folder << ")";
        for (const auto& word : s.command) std::cerr << " " << word;
        std::cerr << "\n";
    }

    std::vector<char*> arguments(const one::driver::step& s) {
        std::vector<char*> out;
        for (const auto& word : s.command) out.push_back(const_cast<char*>(word.c_str()));
        out.push_back(nullptr);
        return out;
    }

    // A step run to its end, said first; whether it succeeded.
    bool perform(const one::driver::step& s) {
        say(s);
        std::cout.flush();
        pid_t child = fork();
        if (child == 0) {
            if (chdir(s.folder.c_str()) == 0) execvp(s.command[0].c_str(), arguments(s).data());
            std::cerr << "one: couldn't run " << s.command[0] << "\n";
            _exit(127);
        }
        int status = 0;
        return child >= 0 && waitpid(child, &status, 0) >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }

    // Builds the project, then each step of running its app in turn, and then the app
    // in this process's place, so closing it, or Ctrl+C, is closing the app.
    int run(const std::string& project, const std::string& environment, const std::string& target) {
        std::string out = project + "/build";
        one::driver::built built;
        if (int wrote = build(project, out, environment, target, &built); wrote != 0) return wrote;
        auto plan = one::driver::plan_run(built, out, target, installed("yarn"));
        if (!plan.refusal.empty()) {
            std::cerr << "one: " << plan.refusal << "\n";
            return 1;
        }
        for (const auto& s : plan.before) {
            if (!perform(s)) {
                std::cerr << "one: " << s.command[0] << " failed, so the " << plan.target << " app wasn't started\n";
                return 1;
            }
        }
        say(plan.app);
        std::cout.flush();
        if (chdir(plan.app.folder.c_str()) == 0) execvp(plan.app.command[0].c_str(), arguments(plan.app).data());
        std::cerr << "one: couldn't start " << plan.app.command[0] << "\n";
        return 1;
    }

    // Builds every project under the folder, each by this compiler run again so a
    // project for another version is handed to it, and deploys none unless all of
    // them build; then deploys each in turn, stopping at the first that fails.
    int deploy(const std::string& root, const std::string& self, const std::string& environment, bool yes) {
        auto plan = one::driver::plan_deploy(root, self, environment, yes);
        if (!plan.refusal.empty()) {
            std::cerr << "one: " << plan.refusal << "\n";
            return 1;
        }
        for (const auto& s : plan.builds) {
            if (!perform(s)) {
                std::cerr << "one: " << s.command[2] << " didn't build, so nothing was deployed\n";
                return 1;
            }
        }
        for (std::size_t i = 0; i < plan.deploys.size(); ++i) {
            if (!perform(plan.deploys[i])) {
                std::cerr << "one: " << plan.builds[i].command[2] << " wasn't deployed";
                if (i + 1 < plan.deploys.size()) {
                    std::cerr << ", so the rest weren't either:";
                    for (std::size_t rest = i + 1; rest < plan.builds.size(); ++rest) std::cerr << " " << plan.builds[rest].command[2];
                }
                std::cerr << "\n";
                return 1;
            }
        }
        std::cout << "deployed " << plan.deploys.size() << (plan.deploys.size() == 1 ? " project\n" : " projects\n");
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
    if (!args.empty() && args[0] == "theme") return theme(args);

    // A project for another compiler is handed to it, before anything is read.
    std::vector<std::string> roots;
    if (!args.empty() && (args[0] == "check" || args[0] == "build" || args[0] == "run")) {
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
    if (args.size() >= 2 && args.size() % 2 == 0 && args[0] == "build") {
        std::string out, environment, targets;
        for (std::size_t i = 2; i < args.size(); i += 2) {
            std::string& value = args[i] == "--out" ? out : args[i] == "--for" ? environment : args[i] == "--target" ? targets : out;
            if ((args[i] != "--out" && args[i] != "--for" && args[i] != "--target") || !value.empty() || args[i + 1].empty()) return usage();
            value = args[i + 1];
        }
        return build(args[1], out, environment, targets);
    }
    if (args.size() >= 2 && args.size() % 2 == 0 && args[0] == "run") {
        std::string environment, target;
        for (std::size_t i = 2; i < args.size(); i += 2) {
            std::string& value = args[i] == "--for" ? environment : target;
            if ((args[i] != "--for" && args[i] != "--target") || !value.empty() || args[i + 1].empty()) return usage();
            value = args[i + 1];
        }
        return run(args[1], environment, target);
    }
    if (args.size() == 2 && args[0] == "show") return show(args[1]);
    if (args.size() >= 2 && args[0] == "deploy") {
        std::string environment;
        bool yes = false;
        for (std::size_t i = 2; i < args.size(); ++i) {
            if (args[i] == "--yes" && !yes) yes = true;
            else if (args[i] == "--for" && environment.empty() && i + 1 < args.size() && !args[i + 1].empty()) environment = args[++i];
            else return usage();
        }
        return deploy(args[1], argv[0], environment, yes);
    }
    return usage();
}
