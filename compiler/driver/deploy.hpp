// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Deploying: a project, or every project under a folder, built and then deployed by
// the deploy script one build writes beside it, one after another.

#pragma once

#include <string>
#include <vector>

#include "driver/run.hpp"

namespace one::driver {

    // The folders under `root`, itself among them, that hold a project block, in a
    // stable order. A project's block is found in its text rather than parsed, since
    // a project may be written for another compiler; a build's own folder is passed
    // over, since nothing there is a project.
    std::vector<std::string> projects_under(const std::string& root);

    struct deploy_plan {
        std::string refusal;        // why there's nothing to deploy, in a sentence
        std::vector<step> builds;   // each project built, all before any is deployed
        std::vector<step> deploys;  // then each deployed, in the same order
    };

    // How to deploy every project under `root`: each built by `one`, the compiler run
    // as `self`, so a project for another version is handed to it, and `--for` its
    // environment when one is named; then each deployed by its build/deploy, asking
    // before it changes anything unless `yes`.
    deploy_plan plan_deploy(const std::string& root, const std::string& self, const std::string& environment, bool yes);

} // namespace one::driver
