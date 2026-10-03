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

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "code/stream.hpp"
#include "generators/web.hpp"
#include "language/ast.hpp"
#include "platform/files.hpp"
#include "version.hpp"

namespace one::generators {

    struct deploy_settings {
        std::string name;
        std::string domain;
        std::string firebase;
        std::string region;
        code::source from;  // the project block they're written in
        std::string github;  // where GitHub's webhook comes in, which needs a secret
        std::vector<std::pair<std::string, std::string>> redirects;  // each address, and where it goes
    };

    static std::string github_route(const std::vector<language::declaration>& declarations) {
        for (const auto& d : declarations) {
            if (auto* n = std::get_if<language::namespace_declaration>(&d.node)) {
                if (auto route = github_route(n->declarations); !route.empty()) return route;
            }
            if (auto* w = std::get_if<language::webhook_declaration>(&d.node); w && w->provider == "github") return w->route;
        }
        return "";
    }

    // The project's deploy settings, when it has all of them.
    inline std::optional<deploy_settings> deploy_settings_of(const std::vector<language::file>& files) {
        for (const auto& f : files) {
            for (const auto& d : f.declarations) {
                auto* p = std::get_if<language::project_declaration>(&d.node);
                if (!p) continue;
                deploy_settings s;
                s.name = p->name;
                s.from = {f.path, d.where.line};
                for (const auto& setting : p->settings) {
                    if (setting.key == "domain") s.domain = setting.value;
                    if (setting.key == "firebase") s.firebase = setting.value;
                    if (setting.key == "region") s.region = setting.value;
                    if (setting.key == "redirect") s.redirects.emplace_back(setting.value, setting.to);
                }
                if (s.domain.empty() || s.firebase.empty() || s.region.empty()) return std::nullopt;
                for (const auto& other : files) {
                    if (s.github.empty()) s.github = github_route(other.declarations);
                }
                return s;
            }
        }
        return std::nullopt;
    }

    namespace infrastructure_detail {

        inline std::string quoted(const std::string& text) { return "\"" + text + "\""; }

        // The folder name a project was generated from, like site/.
        inline std::string source_name(const std::string& project_dir) {
            namespace fs = std::filesystem;
            std::string folder = fs::path(project_dir).lexically_normal().filename().string();
            if (folder.empty()) folder = fs::path(project_dir).lexically_normal().parent_path().filename().string();
            return folder + "/";
        }

        // The folder `name` of the repository the project is in, found by walking up
        // from the project to a folder holding name/marker, as a path relative to `from`.
        inline std::optional<std::string> nearby(const std::string& project_dir, const std::string& from, const std::string& name,
                                                 const std::string& marker = "go.mod") {
            namespace fs = std::filesystem;
            for (fs::path dir = fs::weakly_canonical(project_dir); !dir.empty(); dir = dir.parent_path()) {
                if (fs::exists(dir / name / marker)) return platform::relative_import(from, (dir / name).string());
                if (dir == dir.parent_path()) break;
            }
            return std::nullopt;
        }

    } // namespace infrastructure_detail

    struct generated_infrastructure {
        std::vector<output_file> files;  // inside the build folder, like infrastructure/main.go
        std::string skipped;             // why no deploy was written, for a project that has settings
    };

    inline generated_infrastructure generate_infrastructure(const std::vector<language::file>& files,
                                                            const std::string& project_dir, const std::string& out_dir) {
        using code::stream;
        using infrastructure_detail::quoted;
        auto settings = deploy_settings_of(files);
        if (!settings) return {};
        const auto& s = *settings;
        // Inside a clone of the uione repository, a deploy builds the library from it;
        // anywhere else, from the release the compiler belongs to.
        auto library = infrastructure_detail::nearby(project_dir, out_dir + "/infrastructure", "infrastructure");
        std::string from = infrastructure_detail::source_name(project_dir);
        std::string generated = "# Generated from " + from + " by one. Do not edit.";
        std::vector<output_file> out;

        {
            stream y;
            auto from_project = y.from(s.from.path, s.from.line);  // all of this is the project block's doing
            y.line(generated);
            y.line();
            y.line("name: " + s.name);
            y.line("description: Where " + s.name + " runs, in Google Cloud.");
            y.line("runtime: go");
            y.open("config:");
            y.line("gcp:project: " + s.firebase);
            y.line("# Budgets are billed to a project, and with a person's credentials, rather than");
            y.line("# a service account's, the provider has to be told which one.");
            y.line("gcp:billingProject: " + s.firebase);
            y.line("gcp:userProjectOverride: true");
            out.push_back(file("infrastructure/Pulumi.yaml", y));
        }
        {
            stream m("\t");
            auto from_project = m.from(s.from.path, s.from.line);  // all of this is the project block's doing
            m.line("module " + s.domain + "/infrastructure");
            m.line();
            m.line("go 1.26");
            m.line();
            m.line("require github.com/da0x/uione/infrastructure v" + std::string(version));
            if (library) {
                m.line();
                m.line("replace github.com/da0x/uione/infrastructure => " + *library);
            }
            out.push_back(file("infrastructure/go.mod", m));
        }
        {
            stream g("\t");
            auto from_project = g.from(s.from.path, s.from.line);  // all of this is the project block's doing
            g.generated_from(from);
            g.line("// Everything " + s.name + " runs on. The deploy script next to infrastructure/ runs it.");
            g.line("package main");
            g.line();
            g.line("import \"github.com/da0x/uione/infrastructure\"");
            g.line();
            g.open("func main() {");
            g.open("infrastructure.Deploy(infrastructure.Project{");
            g.line("Name:     " + quoted(s.name) + ",");
            g.line("Domain:   " + quoted(s.domain) + ",");
            g.line("Firebase: " + quoted(s.firebase) + ",");
            g.line("Region:   " + quoted(s.region) + ",");
            if (!s.github.empty()) g.line("GitHub:   " + quoted(s.github) + ",");
            g.close("})");
            g.close("}");
            out.push_back(file("infrastructure/main.go", g));
        }
        {
            stream d;
            auto from_project = d.from(s.from.path, s.from.line);  // all of this is the project block's doing
            d.line(generated);
            d.line("#");
            d.line("# Builds the backend. When its go.mod points the one library at a folder, the");
            d.line("# deploy puts that folder beside it, in one/, and the build uses it.");
            d.line("FROM golang:1.26 AS build");
            d.line("WORKDIR /src");
            d.line("COPY . .");
            d.line("WORKDIR /src/api");
            d.line("RUN if [ -d ../one ]; then go mod edit -replace github.com/da0x/uione/one=../one; fi \\");
            d.line("    && go mod tidy && CGO_ENABLED=0 go build -o /backend .");
            d.line();
            d.line("FROM gcr.io/distroless/static-debian12:nonroot");
            d.line("COPY --from=build /backend /backend");
            d.line("ENTRYPOINT [\"/backend\"]");
            out.push_back(file("api/dockerfile", d));
        }
        {
            // Hosting serves the app, and sends /api/** to the backend, so the app calls
            // it at the same address in the cloud as it does locally through Vite.
            stream j;
            auto from_project = j.from(s.from.path, s.from.line);  // all of this is the project block's doing
            j.open("{");
            j.open("\"hosting\": {");
            j.line("\"public\": \"dist\",");
            // Pages are checked for a newer version every time, so a deploy shows at once.
            // What they load has a hash of its content in its name, so it's kept for good.
            j.open("\"headers\": [");
            j.line("{ \"source\": \"**\", \"headers\": [{ \"key\": \"Cache-Control\", \"value\": \"no-cache\" }] },");
            j.line("{ \"source\": \"/assets/**\", \"headers\": [{ \"key\": \"Cache-Control\", \"value\": \"public, max-age=31536000, immutable\" }] }");
            j.close("],");
            // An address that has moved, like the installer, sends whoever asks for it on.
            if (!s.redirects.empty()) {
                j.open("\"redirects\": [");
                for (std::size_t i = 0; i < s.redirects.size(); ++i) {
                    j.line("{ \"source\": " + quoted(s.redirects[i].first) + ", \"destination\": " + quoted(s.redirects[i].second) +
                           ", \"type\": 301 }" + (i + 1 < s.redirects.size() ? "," : ""));
                }
                j.close("],");
            }
            j.open("\"rewrites\": [");
            j.line("{ \"source\": \"/api/**\", \"run\": { \"serviceId\": \"api\", \"region\": " + quoted(s.region) + " } },");
            if (!s.github.empty()) j.line("{ \"source\": \"/hooks/**\", \"run\": { \"serviceId\": \"api\", \"region\": " + quoted(s.region) + " } },");
            j.line("{ \"source\": \"**\", \"destination\": \"/index.html\" }");
            j.close("]");
            j.close("}");
            j.close("}");
            out.push_back(file("web/firebase.json", j));
        }
        {
            stream sh;
            auto from_project = sh.from(s.from.path, s.from.line);  // all of this is the project block's doing
            sh.line("#!/bin/sh");
            sh.line(generated);
            sh.line("#");
            sh.line("# Deploys " + s.name + ": everything in Google Cloud through Pulumi, then the web app");
            sh.line("# to Firebase Hosting, through the infrastructure library's deployer. It asks");
            sh.line("# before Pulumi changes anything, and it can be run from any folder. --yes deploys");
            sh.line("# without asking, and --json reports each step as a line of JSON.");
            sh.line();
            sh.line("set -eu");
            sh.line("here=$(cd \"$(dirname \"$0\")\" && pwd)");
            sh.line();
            sh.line("cd \"$here/infrastructure\"");
            sh.line("go mod tidy");
            sh.line("GOFLAGS=-mod=mod exec go run github.com/da0x/uione/infrastructure/deployer --stack production --build \"$here\" \"$@\"");
            out.push_back(file("deploy", sh, true));
        }
        return {out, ""};
    }

} // namespace one::generators
