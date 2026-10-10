// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Everything one project generates, in one place, so `one build` writes it and
// `one show` reads it the same way.

#pragma once

#include <string>
#include <vector>

#include "generators/api.hpp"
#include "generators/cpp.hpp"
#include "generators/infrastructure.hpp"
#include "generators/indexes.hpp"
#include "generators/roles.hpp"
#include "generators/rules.hpp"
#include "generators/web.hpp"
#include "language/ast.hpp"
#include "language/diagnostics.hpp"

namespace one::generators {

    struct generated_project {
        std::vector<output_file> files;  // inside the build folder, like web/src/app.tsx
        language::diagnostics errors;    // what the backend can't do yet; nothing is written then
        std::string note;                // something worth saying that isn't an error
    };

    // The project's files have parsed and checked cleanly. `out_dir` is where the
    // build folder goes, since imports between it and the project are worked out.
    // The views that say who holds which roles (roles.hpp) are added to the files
    // first, so every generator sees them.
    inline generated_project generate_project(std::vector<language::file>& files, const std::string& project_dir,
                                              const std::string& out_dir) {
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
        under("web", generate_web(files, project_dir, out_dir + "/web"));
        under("api", api.files);
        under("", {rules_file(project_dir), indexes_file(files), generate_cpp(files, project_dir)});
        auto deploy = generate_infrastructure(files, project_dir, out_dir);
        out.note = deploy.skipped;
        under("", deploy.files);
        return out;
    }

} // namespace one::generators
