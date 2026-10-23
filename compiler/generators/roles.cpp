// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "generators/roles.hpp"

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

    namespace roles_detail {

        struct found {
            std::map<std::string, const language::entity_declaration*> entities;  // by namespace::name
            std::vector<std::pair<std::string, const language::role_declaration*>> roles;  // with their namespace
            std::vector<std::pair<std::string, const language::roles_declaration*>> defined;  // with their namespace
            std::set<std::string> open;  // commands that say who may run them, like permission signed_in
        };

        inline std::string join(const std::string& ns, const std::string& name) { return ns.empty() ? name : ns + "::" + name; }

        inline void collect(const std::vector<language::declaration>& declarations, const std::string& ns, found& out) {
            for (const auto& d : declarations) {
                if (auto* n = std::get_if<language::namespace_declaration>(&d.node)) collect(n->declarations, join(ns, n->name), out);
                if (auto* e = std::get_if<language::entity_declaration>(&d.node)) out.entities[join(ns, e->name)] = e;
                if (auto* r = std::get_if<language::role_declaration>(&d.node)) out.roles.emplace_back(ns, r);
                if (auto* r = std::get_if<language::roles_declaration>(&d.node)) out.defined.emplace_back(ns, r);
                if (auto* c = std::get_if<language::command_declaration>(&d.node)) {
                    for (const auto& st : c->body) {
                        if (std::holds_alternative<language::permission_statement>(st.node)) out.open.insert(join(ns, c->name.text()));
                    }
                }
            }
        }

    } // namespace roles_detail

    role_views roles_of(const std::vector<language::file>& files) {
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
                out.held.push_back({roles_detail::join(ns, name), *role->per, place, {}, "", "", {}, ""});
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
        // Roles each project defines: the view of a person's roles also holds what
        // each of them allows now, read through the role's record.
        for (const auto& [ns, defined] : found.defined) {
            auto granted = found.entities.find(roles_detail::join(ns, defined->from));
            if (granted == found.entities.end()) continue;
            const language::entity_declaration& member = *granted->second;
            std::string person, place, role;
            for (const auto& f : member.fields) {
                std::string type = f.type ? f.type->parts.back() : "";
                if (type == "user" && person.empty()) person = f.name;
                if (type == defined->per && place.empty()) place = f.name;
                if (type == defined->entity && role.empty()) role = f.name;
            }
            auto record = found.entities.find(roles_detail::join(ns, defined->entity));
            std::string allows;
            if (record != found.entities.end()) {
                for (const auto& f : record->second->fields) {
                    if (f.list && f.type && f.type->text() == "permission") allows = f.name;
                }
            }
            if (person.empty() || place.empty() || role.empty() || allows.empty()) continue;
            std::string name = member.name + "_roles";
            std::string open, close;
            for (std::size_t start = 0; !ns.empty() && start <= ns.size();) {
                std::size_t end = ns.find("::", start);
                open += "namespace " + ns.substr(start, end == std::string::npos ? std::string::npos : end - start) + " {\n";
                close += "}\n";
                if (end == std::string::npos) break;
                start = end + 2;
            }
            std::string source = open + "view " + name + " per user {\n\teach " + member.name + " where " + person +
                                 " == user.id {\n\t\t" + place + "  " + role + "  " + role + "." + allows + "\n\t}\n}\n" + close;
            language::diagnostics ignored;
            out.files.push_back(language::parse("<" + name + ">", source, ignored));
            out.held.push_back({roles_detail::join(ns, name), defined->per, place, {}, role + "." + allows, ns, found.open, defined->entity});
        }
        return out;
    }

} // namespace one::generators
