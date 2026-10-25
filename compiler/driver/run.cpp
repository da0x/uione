// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "driver/run.hpp"

#include <algorithm>
#include <regex>

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
            plan.before.push_back({{yarn ? "yarn" : "npm", "install"}, folder});
            plan.app = {yarn ? std::vector<std::string>{"yarn", "dev"} : std::vector<std::string>{"npm", "run", "dev"}, folder};
            return plan;
        }
        if (plan.target == "desktop" || plan.target == "terminal") {
            std::string program = program_of(b, plan.target);
            if (program.empty()) {
                plan.refusal = "the " + plan.target + " app's CMakeLists.txt doesn't name its program";
                return plan;
            }
            plan.before.push_back({{"cmake", "-S", folder, "-B", folder + "/out"}, "."});
            plan.before.push_back({{"cmake", "--build", folder + "/out"}, "."});
            plan.app = {{folder + "/out/" + program}, "."};
            return plan;
        }
        plan.refusal = "one run doesn't run a " + plan.target + " app";
        return plan;
    }

} // namespace one::driver
