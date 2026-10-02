// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Writes a project's Firestore security rules. Browsers only ever read views, and
// every view document says who may read it, so the rules are the same for every
// project: they read that answer off the document.

#pragma once

#include <algorithm>
#include <filesystem>
#include <string>

#include "code/stream.hpp"
#include "generators/web.hpp"

namespace one::generators {

    inline std::string generate_rules(const std::string& project_dir) {
        std::string folder = std::filesystem::path(project_dir).lexically_normal().filename().string();
        if (folder.empty()) folder = std::filesystem::path(project_dir).lexically_normal().parent_path().filename().string();

        code::stream out;
        auto fixed = out.fixed();  // the same for every project
        out.line("rules_version = '2';");
        out.line();
        out.line("// Generated from " + folder + "/ by one. Do not edit.");
        out.line("//");
        out.line("// A browser reads views and nothing else, and never writes: every change goes");
        out.line("// through a command on the server. Each view document says who may read it.");
        out.line();
        out.open("service cloud.firestore {");
        out.open("match /databases/{database}/documents {");
        out.open("function signedIn() {");
        out.line("return request.auth != null;");
        out.close("}");
        out.line();
        out.line("// A person's role is users/{id}.role_id, and roles/{role}.permissions lists what");
        out.line("// it grants. Read on every check, so a change of role takes effect at once.");
        out.open("function granted(permission) {");
        out.line("return signedIn()");
        out.line("  && exists(/databases/$(database)/documents/users/$(request.auth.uid))");
        out.line("  && get(/databases/$(database)/documents/users/$(request.auth.uid)).data.get('role_id', '') != ''");
        out.line("  && get(/databases/$(database)/documents/roles/$(get(/databases/$(database)/documents/users/$(request.auth.uid)).data.role_id)).data.permissions.hasAny([permission]);");
        out.close("}");
        out.line();
        out.open("match /views/{view} {");
        out.line("// A view that doesn't exist yet reads as empty, rather than as an error. A");
        out.line("// document of a private project lists its members as its readers.");
        out.line("allow read: if resource == null");
        out.line("  || resource.data.public == true");
        out.line("  || (signedIn() && resource.data.owner_uid == request.auth.uid)");
        out.line("  || (signedIn() && request.auth.uid in resource.data.get('readers', []))");
        out.line("  || (resource.data.required_permission != '' && granted(resource.data.required_permission));");
        out.line("allow write: if false;");
        out.close("}");
        out.line();
        out.open("match /{document=**} {");
        out.line("allow read, write: if false;");
        out.close("}");
        out.close("}");
        out.close("}");
        return out.str();
    }

    // The rules as a generated file. Every line is the same for every project.
    inline output_file rules_file(const std::string& project_dir) {
        output_file f{"firestore.rules", generate_rules(project_dir)};
        f.sources.assign(static_cast<std::size_t>(std::count(f.content.begin(), f.content.end(), '\n')), code::source{});
        return f;
    }

} // namespace one::generators
