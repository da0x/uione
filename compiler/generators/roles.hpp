// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What a page needs to know to show a person only the buttons they may press: the
// roles they hold, and where. A role held within something, like
//
//     role maintainer per project from member  issue::close
//
// is held by whoever a member names, in the project it points at. For each entity
// that grants roles this way, the build adds a view per person of what it grants
// them, as if the project had said
//
//     view member_roles per user {
//         each member where person == user.id {
//             project  role
//         }
//     }
//
// Only that person reads it, and it's kept current like any other view. It's added
// after the project is checked, for the generators alone: it isn't the project's
// own, so the editor never shows it.

#pragma once

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "language/ast.hpp"
#include "language/diagnostics.hpp"
#include "language/parser.hpp"

namespace one::generators {

    // Where a role is held, as a page reads it: the view of the person's grants, the
    // field naming where, like project, and the roles that grant a command there.
    struct held_roles {
        std::string view;                 // like projects::member_roles
        std::string within;               // the entity the roles are held in, like project
        std::string field;                // the grant's field naming it, like project
        std::map<std::string, std::vector<std::string>> granting;  // command, like projects::issue::close, and the roles that grant it
    };

    namespace roles_detail {

        struct found {
            std::map<std::string, const language::entity_declaration*> entities;  // by namespace::name
            std::vector<std::pair<std::string, const language::role_declaration*>> roles;  // with their namespace
        };

        inline std::string join(const std::string& ns, const std::string& name) { return ns.empty() ? name : ns + "::" + name; }

        inline void collect(const std::vector<language::declaration>& declarations, const std::string& ns, found& out) {
            for (const auto& d : declarations) {
                if (auto* n = std::get_if<language::namespace_declaration>(&d.node)) collect(n->declarations, join(ns, n->name), out);
                if (auto* e = std::get_if<language::entity_declaration>(&d.node)) out.entities[join(ns, e->name)] = e;
                if (auto* r = std::get_if<language::role_declaration>(&d.node)) out.roles.emplace_back(ns, r);
            }
        }

    } // namespace roles_detail

    // The roles held within something, each granted by an entity, and the source of
    // the views that say who holds them.
    struct role_views {
        std::vector<held_roles> held;
        std::vector<language::file> files;  // the views, as parsed .one
    };

    inline role_views roles_of(const std::vector<language::file>& files) {
        roles_detail::found found;
        for (const auto& f : files) roles_detail::collect(f.declarations, "", found);
        role_views out;
        std::map<std::string, std::size_t> by_grant;  // namespace::member, to its place in held
        for (const auto& [ns, role] : found.roles) {
            if (!role->per || !role->from) continue;
            auto granted = found.entities.find(roles_detail::join(ns, *role->from));
            if (granted == found.entities.end()) continue;
            const language::entity_declaration& member = *granted->second;
            std::string person, place;
            bool has_role = false;
            for (const auto& f : member.fields) {
                std::string type = f.type ? f.type->parts.back() : "";
                if (type == "user" && person.empty()) person = f.name;
                if (type == *role->per && place.empty()) place = f.name;
                if (f.name == "role") has_role = true;
            }
            if (person.empty() || place.empty() || !has_role) continue;
            std::string key = roles_detail::join(ns, member.name);
            auto [at, added] = by_grant.emplace(key, out.held.size());
            if (added) {
                std::string name = member.name + "_roles";
                out.held.push_back({roles_detail::join(ns, name), *role->per, place, {}});
                // Inside the namespaces the member is in, as its own file.
                std::string open, close;
                for (std::size_t start = 0; !ns.empty() && start <= ns.size();) {
                    std::size_t end = ns.find("::", start);
                    open += "namespace " + ns.substr(start, end == std::string::npos ? std::string::npos : end - start) + " {\n";
                    close += "}\n";
                    if (end == std::string::npos) break;
                    start = end + 2;
                }
                std::string source = open + "view " + name + " per user {\n\teach " + member.name + " where " + person +
                                     " == user.id {\n\t\t" + place + "  role\n\t}\n}\n" + close;
                language::diagnostics ignored;
                out.files.push_back(language::parse("<" + name + ">", source, ignored));
            }
            for (const auto& permission : role->permissions) {
                // issue::close, as the role writes it, is the command in its namespace.
                std::string command = roles_detail::join(ns, permission.text());
                auto& granting = out.held[at->second].granting[command];
                if (std::find(granting.begin(), granting.end(), role->name) == granting.end()) granting.push_back(role->name);
            }
        }
        return out;
    }

} // namespace one::generators
