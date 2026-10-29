// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "driver/deploy.hpp"

#include <algorithm>
#include <filesystem>
#include <regex>
#include <string>
#include <vector>

#include "platform/files.hpp"

namespace one::driver {

    std::vector<std::string> projects_under(const std::string& root) {
        namespace fs = std::filesystem;
        static const std::regex block(R"re((^|\n)[ \t]*project[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*\{)re");
        std::vector<std::string> found;
        for (const auto& path : platform::find_one_files({root})) {
            fs::path folder = fs::path(path).parent_path();
            auto relative = fs::path(path).lexically_relative(root);
            if (std::ranges::any_of(relative, [](const fs::path& part) { return part == "build" || part == "node_modules"; })) continue;
            auto text = platform::read_file(path);
            if (!text || !std::regex_search(*text, block)) continue;
            found.push_back(folder.empty() ? "." : folder.string());
        }
        std::ranges::sort(found);
        found.erase(std::unique(found.begin(), found.end()), found.end());
        return found;
    }

    deploy_plan plan_deploy(const std::string& root, const std::string& self, const std::string& environment, bool yes) {
        deploy_plan plan;
        auto projects = projects_under(root);
        if (projects.empty()) {
            plan.refusal = "there's no project under " + root + ", no .one file there has a project block";
            return plan;
        }
        for (const auto& project : projects) {
            step build{{self, "build", project}, "."};
            if (!environment.empty()) build.command.insert(build.command.end(), {"--for", environment});
            plan.builds.push_back(std::move(build));
            step deploy{{(std::filesystem::path(project) / "build" / "deploy").string()}, "."};
            if (yes) deploy.command.push_back("--yes");
            plan.deploys.push_back(std::move(deploy));
        }
        return plan;
    }

} // namespace one::driver
