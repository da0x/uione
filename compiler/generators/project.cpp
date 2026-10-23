// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "generators/project.hpp"

#include <string>
#include <vector>

#include "generators/agent.hpp"
#include "generators/api.hpp"
#include "generators/cpp.hpp"
#include "generators/infrastructure.hpp"
#include "generators/native.hpp"
#include "generators/indexes.hpp"
#include "generators/roles.hpp"
#include "generators/rules.hpp"
#include "generators/web.hpp"
#include "language/targets.hpp"

namespace one::generators {

    generated_project generate_project(std::vector<language::file>& files, const std::string& project_dir,
                                       const std::string& out_dir, const std::vector<std::string>& targets) {
        for (auto& view : roles_of(files).files) files.push_back(std::move(view));
        generated_project out;
        auto api = generate_api(files, project_dir, out_dir + "/api");
        out.errors = api.errors;
        if (!out.errors.empty()) return out;
        auto under = [&](const std::string& folder, std::vector<output_file> written) {
            for (auto& f : written) {
                f.path = folder.empty() ? f.path : folder + "/" + f.path;
                out.files.push_back(std::move(f));
            }
        };
        // Each app the project's targets name, from its screens.
        if (language::listed(targets, "web")) under("web", generate_web(files, project_dir, out_dir + "/web"));
        for (const char* native : {"desktop", "terminal"}) {
            if (language::listed(targets, native)) under(native, generate_native(files, project_dir, native));
        }
        if (needs_backend(files)) under("api", api.files);
        under("", generate_agents(files));
        under("", {rules_file(project_dir), indexes_file(files), generate_cpp(files, project_dir)});
        auto deploy = generate_infrastructure(files, project_dir, out_dir);
        out.note = deploy.skipped;
        under("", deploy.files);
        return out;
    }

} // namespace one::generators
