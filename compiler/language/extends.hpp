// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

// A project that extends another, like a hosted edition built on an open-source app:
// `extends tracker` in its project block. Its files and the other's are one project,
// checked and built together; its project block says what it is, and takes from the
// other's whatever it doesn't say itself.

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "language/ast.hpp"
#include "platform/files.hpp"

namespace one::language {

    // The folder a project extends, from the folder its project block is in: a name
    // is a project beside it, like extends tracker; a quoted path is where it says,
    // like extends "../tracker".
    inline std::string extended_folder(const std::string& dir, const setting& s) {
        std::string from = dir.empty() ? "." : dir;
        return platform::resolve(from, s.is_string ? s.value : "../" + s.value);
    }

    // The folder a file is in.
    inline std::string folder_of(const std::string& path) { return std::filesystem::path(path).parent_path().string(); }

    namespace extends_detail {

        // A setting that names a file or folder beside its .one files, which is found
        // from wherever the project block it comes into is.
        inline bool names_a_path(const std::string& key) { return key == "icon" || key == "serve"; }

        inline std::string relative_to(const std::string& path, const std::string& dir) {
            namespace fs = std::filesystem;
            std::error_code ignored;
            fs::path to = fs::weakly_canonical(fs::absolute(path, ignored), ignored);
            fs::path from = fs::weakly_canonical(fs::absolute(dir.empty() ? "." : dir, ignored), ignored);
            return to.lexically_relative(from).generic_string();
        }

    } // namespace extends_detail

    // Folds the project blocks of the projects one extends into its own: each setting
    // and environment it doesn't say is taken from the nearest that does, a path read
    // from where that one is. Their blocks go, so the project has one. Nothing changes
    // when no project block extends another.
    inline void inherit_projects(std::vector<file>& files) {
        namespace fs = std::filesystem;
        struct block {
            file* in;
            std::size_t at;
            project_declaration* p;
        };
        std::vector<block> blocks;
        for (auto& f : files) {
            for (std::size_t i = 0; i < f.declarations.size(); ++i) {
                if (auto* p = std::get_if<project_declaration>(&f.declarations[i].node)) blocks.push_back({&f, i, p});
            }
        }
        auto canonical = [](const std::string& dir) {
            std::error_code ignored;
            return fs::weakly_canonical(fs::absolute(dir.empty() ? "." : dir, ignored), ignored).string();
        };
        auto extends_of = [](const project_declaration& p) -> const setting* {
            auto it = std::find_if(p.settings.begin(), p.settings.end(), [](const setting& s) { return s.key == "extends"; });
            return it == p.settings.end() ? nullptr : &*it;
        };
        // What each block extends, and so which one nothing extends: the project's own.
        std::set<std::string> extended;
        for (const auto& b : blocks) {
            if (const setting* s = extends_of(*b.p)) extended.insert(canonical(extended_folder(folder_of(b.in->path), *s)));
        }
        if (extended.empty()) return;
        block* own = nullptr;
        for (auto& b : blocks) {
            if (extends_of(*b.p) && !extended.contains(canonical(folder_of(b.in->path)))) own = &b;
        }
        if (!own) return;  // they extend each other, which the driver says
        const std::string here = folder_of(own->in->path);
        // Nearest first: the one it extends, then the one that one extends.
        std::vector<block*> chain;
        std::set<std::string> seen{canonical(here)};
        for (block* at = own; const setting* s = extends_of(*at->p);) {
            std::string next = canonical(extended_folder(folder_of(at->in->path), *s));
            if (seen.contains(next)) break;
            seen.insert(next);
            auto found = std::find_if(blocks.begin(), blocks.end(), [&](const block& b) { return canonical(folder_of(b.in->path)) == next; });
            if (found == blocks.end()) break;
            chain.push_back(&*found);
            at = &*found;
        }
        project_declaration& mine = *own->p;
        for (block* b : chain) {
            std::set<std::string> said;
            for (const auto& s : mine.settings) said.insert(s.key);
            for (const auto& s : b->p->settings) {
                if (said.contains(s.key) || s.key == "one" || s.key == "extends") continue;
                setting taken = s;
                if (extends_detail::names_a_path(s.key)) {
                    taken.value = extends_detail::relative_to(platform::resolve(folder_of(b->in->path), s.value), here);
                }
                mine.settings.push_back(std::move(taken));
            }
            for (const auto& e : b->p->environments) {
                bool named = std::any_of(mine.environments.begin(), mine.environments.end(), [&](const auto& m) { return m.name == e.name; });
                if (!named) mine.environments.push_back(e);
            }
        }
        // Their blocks go, the last in each file first, so the places stay right.
        std::sort(chain.begin(), chain.end(), [](const block* a, const block* b) { return a->in == b->in ? a->at > b->at : a->in < b->in; });
        for (block* b : chain) b->in->declarations.erase(b->in->declarations.begin() + static_cast<std::ptrdiff_t>(b->at));
    }

} // namespace one::language
