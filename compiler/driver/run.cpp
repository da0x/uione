// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "driver/run.hpp"

#include <algorithm>
#include <regex>
#include <string>
#include <thread>

namespace one::driver {

    namespace {

        // The program a native app's CMakeLists.txt builds: its OUTPUT_NAME, the
        // project's name.
        std::string program_of(const built& b, const std::string& target) {
            for (const auto& f : b.files) {
                if (f.path != target + "/CMakeLists.txt") continue;
                std::smatch m;
                if (std::regex_search(f.content, m, std::regex(R"(OUTPUT_NAME\s+([^\s)]+))"))) return m[1];
            }
            return "";
        }

    } // namespace

    run_plan plan_run(const built& b, const std::string& out, const std::string& target, bool yarn) {
        run_plan plan;
        plan.target = target.empty() && !b.targets.empty() ? b.targets.front() : target;
        if (std::ranges::find(b.targets, plan.target) == b.targets.end()) {
            plan.refusal = "this project doesn't build " + plan.target;
            return plan;
        }
        std::string folder = out + "/" + plan.target;
        if (plan.target == "web") {
            // Libraries from a checkout are copied in by Yarn, which keeps its copy until
            // their version changes, so it's made again each time, a change in them
            // since the last run being what's served; npm links them, so it needn't.
            bool checkout = std::ranges::any_of(b.files, [](const auto& f) { return f.path == "web/package.json" && f.content.find("\"file:") != std::string::npos; });
            if (yarn && checkout) plan.before.push_back({{"yarn", "install", "--force"}, folder});
            else plan.before.push_back({{yarn ? "yarn" : "npm", "install"}, folder});
            // Served with Vite's own copy of the packages made again, so a uione library
            // changed since the last run, like a checkout's, is what's served.
            plan.app = {yarn ? std::vector<std::string>{"yarn", "dev", "--force"} : std::vector<std::string>{"npm", "run", "dev", "--", "--force"}, folder};
            return plan;
        }
        if (plan.target == "desktop" || plan.target == "terminal") {
            std::string program = program_of(b, plan.target);
            if (program.empty()) {
                plan.refusal = "the " + plan.target + " app's CMakeLists.txt doesn't name its program";
                return plan;
            }
            plan.before.push_back({{"cmake", "-S", folder, "-B", folder + "/out"}, "."});
            // On every core, as Make otherwise builds one file at a time.
            std::vector<std::string> build{"cmake", "--build", folder + "/out"};
            if (unsigned cores = std::thread::hardware_concurrency()) build.insert(build.end(), {"--parallel", std::to_string(cores)});
            plan.before.push_back({build, "."});
            plan.app = {{folder + "/out/" + program}, "."};
            return plan;
        }
        plan.refusal = "one run doesn't run a " + plan.target + " app";
        return plan;
    }

} // namespace one::driver
