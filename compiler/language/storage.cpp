// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "language/storage.hpp"

#include <string>
#include <variant>

namespace one::language {

    bool stores_locally(const std::vector<file>& files) {
        for (const auto& f : files) {
            for (const auto& d : f.declarations) {
                auto* p = std::get_if<project_declaration>(&d.node);
                if (!p) continue;
                for (const auto& s : p->settings) {
                    if (s.key == "storage" && s.value == "local") return true;
                }
            }
        }
        return false;
    }

    namespace storage_detail {

        bool says_me(const expression& e);

        bool says_me(const expression_ptr& e) { return e && says_me(*e); }

        bool says_me(const expression& e) {
            return std::visit(
                [](const auto& n) -> bool {
                    using kind = std::decay_t<decltype(n)>;
                    if constexpr (std::is_same_v<kind, name_expression>) return n.name.parts.size() == 1 && n.name.parts[0] == "me";
                    else if constexpr (std::is_same_v<kind, member_expression>) return says_me(n.object);
                    else if constexpr (std::is_same_v<kind, call_expression>) {
                        bool any = says_me(n.callee);
                        for (const auto& a : n.arguments) any = any || says_me(a);
                        return any;
                    } else if constexpr (std::is_same_v<kind, where_expression>) return says_me(n.source) || says_me(n.condition);
                    else if constexpr (std::is_same_v<kind, unary_expression>) return says_me(n.operand);
                    else if constexpr (std::is_same_v<kind, binary_expression>) return says_me(n.left) || says_me(n.right);
                    else if constexpr (std::is_same_v<kind, list_expression>) {
                        bool any = false;
                        for (const auto& i : n.items) any = any || says_me(i);
                        return any;
                    } else if constexpr (std::is_same_v<kind, match_expression>) {
                        bool any = says_me(n.subject) || says_me(n.otherwise);
                        for (const auto& arm : n.arms) any = any || says_me(arm.value);
                        return any;
                    } else return false;
                },
                e.node);
        }

        struct refusing {
            const std::string& path;
            diagnostics& out;

            // Why one machine can't have it: nobody signs in to a local app, it has no
            // server, or local storage doesn't do it yet.
            enum class needs { people, server, later };

            void refuse(location where, const std::string& what, needs why) {
                std::string because = why == needs::people   ? " needs someone signed in, and nobody signs in to a project that says storage local"
                                      : why == needs::server ? " needs a server, and a project that says storage local has none"
                                                             : " isn't something local storage does yet";
                out.push_back({path, where, what + because + "; take it out, or take out storage local"});
            }

            void values(const std::vector<field_value>& given, location where) {
                for (const auto& v : given)
                    if (says_me(v.value)) return refuse(where, "me, who's running a command,", needs::people);
            }

            void statements(const std::vector<statement>& body) {
                for (const auto& s : body) {
                    std::visit(
                        [&](const auto& n) {
                            using kind = std::decay_t<decltype(n)>;
                            if constexpr (std::is_same_v<kind, permission_statement>) {
                                if (n.permission.text() != "anyone") refuse(s.where, "a permission", needs::people);
                            } else if constexpr (std::is_same_v<kind, stripe_statement>) {
                                refuse(s.where, "a payment through Stripe", needs::server);
                            } else if constexpr (std::is_same_v<kind, require_statement>) {
                                if (says_me(n.condition)) refuse(s.where, "me, who's running a command,", needs::people);
                            } else if constexpr (std::is_same_v<kind, assign_statement>) {
                                if (says_me(n.value)) refuse(s.where, "me, who's running a command,", needs::people);
                            } else if constexpr (std::is_same_v<kind, list_statement>) {
                                if (says_me(n.value)) refuse(s.where, "me, who's running a command,", needs::people);
                            } else if constexpr (std::is_same_v<kind, return_statement>) {
                                if (says_me(n.value)) refuse(s.where, "me, who's running a command,", needs::people);
                            } else if constexpr (std::is_same_v<kind, create_statement>) {
                                values(n.values, s.where);
                            } else if constexpr (std::is_same_v<kind, dispatch_statement>) {
                                values(n.values, s.where);
                                statements(n.lists);
                            } else if constexpr (std::is_same_v<kind, if_statement>) {
                                if (says_me(n.condition)) refuse(s.where, "me, who's running a command,", needs::people);
                                statements(n.then_body);
                                statements(n.else_body);
                            } else if constexpr (std::is_same_v<kind, each_statement>) {
                                if (says_me(n.where)) refuse(s.where, "me, who's running a command,", needs::people);
                                statements(n.body);
                            } else if constexpr (std::is_same_v<kind, delete_statement>) {
                                if (says_me(n.where)) refuse(s.where, "me, who's running a command,", needs::people);
                            }
                        },
                        s.node);
                }
            }

            void each(const view_each& e) {
                if (says_me(e.source) || says_me(e.condition)) refuse(e.where, "me, who's reading a view,", needs::people);
                for (const auto& v : e.rows)
                    if (says_me(v.value)) refuse(v.where, "me, who's reading a view,", needs::people);
                for (const auto& inner : e.lists) each(inner);
            }

            void declarations(const std::vector<declaration>& all) {
                for (const auto& d : all) {
                    std::visit(
                        [&](const auto& n) {
                            using kind = std::decay_t<decltype(n)>;
                            if constexpr (std::is_same_v<kind, namespace_declaration>) {
                                declarations(n.declarations);
                            } else if constexpr (std::is_same_v<kind, project_declaration>) {
                                for (const auto& s : n.settings) {
                                    if (s.key == "signin") out.push_back({path, s.where, "nobody signs in to a project that says storage local, which is for one person on their own machine; take signin out"});
                                    if (s.key == "firebase" || s.key == "region") refuse(s.where, "a Firebase project", needs::server);
                                    if (s.key == "target" && s.value == "web")
                                        out.push_back({path, s.where, "the web app of a project that says storage local isn't built yet; it will keep its data in the browser, and until then take web out of target"});
                                }
                                bool targeted = false;
                                for (const auto& s : n.settings) targeted = targeted || s.key == "target";
                                if (!targeted)
                                    out.push_back({path, d.where, "a project that says storage local is built for this machine, so it says which app, like target desktop"});
                                for (const auto& e : n.environments) refuse(e.where, "an environment", needs::server);
                                if (!n.at_most.empty()) refuse(n.at_most.front().where, "a limit on how often", needs::people);
                            } else if constexpr (std::is_same_v<kind, entity_declaration>) {
                                if (n.readers) refuse(n.readers_where, "who may read an entity", needs::people);
                                if (n.public_when) refuse(d.where, "an entity that's public when", needs::people);
                                if (n.history || n.history_of) refuse(d.where, "an entity's history", needs::later);
                                if (n.audit_days > 0) refuse(n.audit_where, "an audit", needs::people);
                                if (n.invites) refuse(n.invites_where, "an invitation", needs::people);
                                if (n.gives) refuse(n.gives_where, "a role given to a group", needs::people);
                                if (n.at_most) refuse(n.at_most->where, "a limit on how many", needs::people);
                            } else if constexpr (std::is_same_v<kind, command_declaration>) {
                                if (n.at_most) refuse(n.at_most->where, "a limit on how often", needs::people);
                                statements(n.body);
                            } else if constexpr (std::is_same_v<kind, extend_command_declaration>) {
                                statements(n.body);
                            } else if constexpr (std::is_same_v<kind, view_declaration>) {
                                if (n.per && *n.per == "user") refuse(d.where, "a view per person", needs::people);
                                if (n.is_public || n.public_when) refuse(d.where, "a public view", needs::people);
                                if (n.readers || !n.reader_people.empty() || !n.reader_roles.empty()) refuse(d.where, "who may read a view", needs::people);
                                for (const auto& v : n.values)
                                    if (says_me(v.value)) refuse(v.where, "me, who's reading a view,", needs::people);
                                for (const auto& e : n.each) each(e);
                            } else if constexpr (std::is_same_v<kind, role_declaration> || std::is_same_v<kind, roles_declaration> ||
                                                 std::is_same_v<kind, define_role_declaration> || std::is_same_v<kind, extend_role_declaration>) {
                                refuse(d.where, "a role", needs::people);
                            } else if constexpr (std::is_same_v<kind, webhook_declaration>) {
                                refuse(d.where, "a webhook", needs::server);
                            } else if constexpr (std::is_same_v<kind, define_service_declaration>) {
                                refuse(d.where, "a service", needs::server);
                            } else if constexpr (std::is_same_v<kind, define_agent_declaration> || std::is_same_v<kind, define_workflow_declaration>) {
                                refuse(d.where, "an agent's work", needs::server);
                            } else if constexpr (std::is_same_v<kind, once_declaration>) {
                                refuse(d.where, "something done once on the server", needs::server);
                            } else if constexpr (std::is_same_v<kind, backend_declaration>) {
                                refuse(d.where, "Go written by hand", needs::server);
                            }
                        },
                        d.node);
                }
            }
        };

    }  // namespace storage_detail

    void check_storage(const std::vector<file>& files, diagnostics& out) {
        if (!stores_locally(files)) return;
        for (const auto& f : files) storage_detail::refusing{f.path, out}.declarations(f.declarations);
    }

}  // namespace one::language
