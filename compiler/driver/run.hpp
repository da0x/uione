// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Running a project: one of the apps it builds, built and started on this machine,
// by the same steps a person would take after one build.

#pragma once

#include <string>
#include <vector>

#include "driver/build.hpp"

namespace one::driver {

    struct step {
        std::vector<std::string> command;  // the program, then its arguments
        std::string folder;                // where it runs
    };

    struct run_plan {
        std::string refusal;       // why there's nothing to run, in a sentence
        std::string target;        // the app it runs
        std::vector<step> before;  // each run in turn, and each has to succeed
        step app;                  // then the app itself, until it's closed
    };

    // How to run a project built into `out`: the target asked for, or else the first
    // the project says. The web app has its packages installed, with yarn when
    // `yarn` says this machine has it and npm when not, and is served by Vite for as
    // long as it runs; a desktop or terminal app is configured and built with CMake,
    // then started.
    run_plan plan_run(const built& b, const std::string& out, const std::string& target, bool yarn);

} // namespace one::driver
