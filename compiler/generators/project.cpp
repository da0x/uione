// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "generators/project.hpp"

#include <string>
#include <vector>

#include "generators/agent.hpp"
#include "generators/api.hpp"
#include "generators/cpp.hpp"
#include "generators/infrastructure.hpp"
#include "generators/local.hpp"
#include "generators/native.hpp"
#include "generators/indexes.hpp"
#include "generators/roles.hpp"
#include "generators/rules.hpp"
#include "generators/web.hpp"
#include "language/storage.hpp"
#include "language/targets.hpp"

namespace one::generators {

    generated_project generate_project(std::vector<language::file>& files, const std::string& project_dir,
                                       const std::string& out_dir, const std::vector<std::string>& targets) {
        generated_project out;
        // A project that keeps its data on this machine is its apps alone: its commands
        // and views run inside them, with no service, rules or cloud (0015). The web
        // app keeps it in the browser, and is served from Hosting when the project
        // says where, with nothing else.
        if (language::stores_locally(files)) {
            auto local = generate_local(files);
            out.errors = std::move(local.errors);
            if (!out.errors.empty()) return out;
            if (language::listed(targets, "web")) {
                for (auto& f : generate_web(files, project_dir, out_dir + "/web")) {
                    f.path = "web/" + f.path;
                    out.files.push_back(std::move(f));
                }
                for (auto& f : local.web) {
                    f.path = "web/" + f.path;
                    out.files.push_back(std::move(f));
                }
                auto deploy = generate_infrastructure(files, project_dir, out_dir);
                out.note = deploy.skipped;
                for (auto& f : deploy.files) out.files.push_back(std::move(f));
            }
            for (const char* native : {"desktop", "terminal"}) {
                if (!language::listed(targets, native)) continue;
                for (auto& f : generate_native(files, project_dir, native)) {
                    f.path = std::string(native) + "/" + f.path;
                    out.files.push_back(std::move(f));
                }
            }
            return out;
        }
        for (auto& view : roles_of(files).files) files.push_back(std::move(view));
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
