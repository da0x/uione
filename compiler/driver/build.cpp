// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "driver/build.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "code/stream.hpp"
#include "driver/check.hpp"
#include "generators/project.hpp"
#include "language/targets.hpp"
#include "platform/files.hpp"

namespace one::driver {

    // The targets --target names, while a build checks and generates, and none after.
    class building_only {
    public:
        explicit building_only(std::optional<std::vector<std::string>> targets) { language::building_targets() = std::move(targets); }
        ~building_only() { language::building_targets().reset(); }
        building_only(const building_only&) = delete;
        building_only& operator=(const building_only&) = delete;
    };

    // Chooses the environment a project is built for: the one named, or with none
    // named, the first. Its settings take the place of the shared ones they name, so
    // what's generated is for that one place. Says what's wrong, or "" when nothing is.
    inline std::string choose_environment(std::vector<language::file>& files, const std::string& wanted, std::string& chosen) {
        for (auto& f : files) {
            for (auto& d : f.declarations) {
                auto* p = std::get_if<language::project_declaration>(&d.node);
                if (!p) continue;
                if (p->environments.empty()) {
                    if (!wanted.empty()) return "this project has no environments, so it's built without --for";
                    return "";
                }
                const language::environment_block* environment = &p->environments.front();
                if (!wanted.empty()) {
                    environment = nullptr;
                    std::string names;
                    for (const auto& e : p->environments) {
                        if (e.name == wanted) environment = &e;
                        names += (names.empty() ? "" : ", ") + e.name;
                    }
                    if (!environment) return "there's no environment " + wanted + "; this project has " + names;
                }
                for (const auto& own : environment->settings) {
                    auto shared = std::find_if(p->settings.begin(), p->settings.end(), [&](const auto& s) { return s.key == own.key; });
                    if (shared != p->settings.end()) *shared = own;
                    else p->settings.push_back(own);
                }
                p->environment = environment->name;
                chosen = environment->name;
                return "";
            }
        }
        if (!wanted.empty()) return "this project has no project block, so it has no environments";
        return "";
    }

    built build(const std::string& project, const std::string& out, const std::string& environment,
                std::optional<std::vector<std::string>> targets) {
        built result;
        building_only only(targets);
        std::vector<language::file> files;
        std::size_t count = check_project(project, result.problems, &files);
        if (targets && count > 0) {
            // --target builds fewer than the project says, never others.
            auto said = language::targets_of(files);
            for (const auto& t : *targets) {
                if (language::listed(said, t)) continue;
                std::string listed;
                for (const auto& s : said) listed += (listed.empty() ? "" : " ") + s;
                result.refusal = "--target names " + t + ", which this project doesn't build; its targets are " + listed;
                return result;
            }
        }
        if (!result.problems.empty() || count == 0) {
            result.refusal = "nothing was built, because the project has errors";
            return result;
        }
        std::string chosen;
        if (auto wrong = choose_environment(files, environment, chosen); !wrong.empty()) {
            result.refusal = wrong;
            return result;
        }
        auto generated = generators::generate_project(files, project, out, targets ? *targets : language::targets_of(files));
        if (!generated.errors.empty()) {
            result.problems = std::move(generated.errors);
            result.refusal = "nothing was built, because the backend can't be generated yet";
            return result;
        }
        result.files = std::move(generated.files);
        result.note = std::move(generated.note);
        if (!chosen.empty() && environment.empty()) {
            result.note = (result.note.empty() ? "" : result.note + "; ") + "built for environment " + chosen + ", the first; --for names another";
        }
        result.environment = chosen;
        return result;
    }

    shown show(const std::string& where) {
        namespace fs = std::filesystem;
        shown result;
        auto colon = where.rfind(':');
        if (colon == std::string::npos) {
            result.understood = false;
            return result;
        }
        std::string path = where.substr(0, colon), range = where.substr(colon + 1);
        int from = 0, to = 0;
        try {
            auto dash = range.find('-');
            from = std::stoi(range.substr(0, dash));
            to = dash == std::string::npos ? from : std::stoi(range.substr(dash + 1));
        } catch (const std::exception&) {
            result.understood = false;
            return result;
        }
        if (from < 1 || to < from) {
            result.understood = false;
            return result;
        }
        std::string project = fs::path(path).parent_path().string();
        if (project.empty()) project = ".";
        std::vector<language::file> files;
        check_project(project, result.problems, &files);
        if (!result.problems.empty()) return result;
        auto generated = generators::generate_project(files, project, project + "/build");
        if (!generated.errors.empty()) {
            result.problems = std::move(generated.errors);
            return result;
        }

        auto wanted = fs::weakly_canonical(path);
        for (const auto& file : generated.files) {
            shown_file found{file.path, {}};
            std::size_t at = 0;
            for (std::size_t n = 0; n < file.sources.size() && at < file.content.size(); ++n) {
                std::size_t end = file.content.find('\n', at);
                const auto& source = file.sources[n];
                bool blank = at == end;
                if (!blank && source && !source->fixed() && source->line >= from && source->line <= to &&
                    fs::weakly_canonical(source->path) == wanted) {
                    found.lines.emplace_back(n + 1, file.content.substr(at, end - at));
                }
                at = end + 1;
            }
            if (!found.lines.empty()) result.files.push_back(std::move(found));
        }
        return result;
    }

} // namespace one::driver
