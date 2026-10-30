// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Writes what a project needs to be deployed: a Pulumi program on the infrastructure
// library, the backend's dockerfile, Hosting's firebase.json, and a deploy script
// that runs them in order. Like the backend, each is only what the project says,
// since the infrastructure library does the rest (decision 0002).
//
// A project is deployed only when its settings say where: firebase, region and
// domain. Without them, none of this is written.

#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "code/stream.hpp"
#include "generators/output.hpp"
#include "language/ast.hpp"

namespace one::generators {

    struct deploy_settings {
        std::string name;
        std::string domain;
        std::string firebase;
        std::string region;
        // The Pulumi stack: an environment's is its Google Cloud project, which is what
        // it is, whatever it's called, so renaming it keeps its stack; a project
        // without environments has one, production.
        std::string stack = "production";
        code::source from;  // the project block they're written in
        std::string github;  // where GitHub's webhook comes in, which needs a secret
        std::string stripe;  // where Stripe's webhook comes in, whose secrets the owner sets
        bool services = false;  // a role is made for services, whose tokens the backend signs
        bool signs_in = false;  // people sign in, so programs on their computers may too, with a token the backend signs
        bool files = false;  // a field holds a file, kept in a bucket of the project's
        bool backend = true;  // there's a backend to run; without one, it's only the web app on Hosting
        std::string redirect;  // a second domain, like www.uione.io, that sends whoever opens it to domain
        std::string site;  // its own Hosting site in a Firebase project it shares, like vectra-da0x
        std::vector<std::pair<std::string, std::string>> redirects;  // each address, and where it goes
    };

    struct generated_infrastructure {
        std::vector<output_file> files;  // inside the build folder, like infrastructure/main.go
        std::string skipped;             // why no deploy was written, for a project that has settings
    };

    // Whether the project needs a backend. One with nothing for it to serve, where no
    // one signs in, like a company's front page, is only its web app: no api/ is
    // written, and its deploy needs no database, no Cloud Run and no billing.
    bool needs_backend(const std::vector<language::file>& files);

    // The project's deploy settings, when it has all of them.
    std::optional<deploy_settings> deploy_settings_of(const std::vector<language::file>& files);

    generated_infrastructure generate_infrastructure(const std::vector<language::file>& files,
                                                     const std::string& project_dir, const std::string& out_dir);

} // namespace one::generators
