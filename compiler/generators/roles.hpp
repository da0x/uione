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

#include <map>
#include <set>
#include <string>
#include <vector>

#include "language/ast.hpp"

namespace one::generators {

    // Where a role is held, as a page reads it: the view of the person's grants, the
    // field naming where, like project, and the roles that grant a command there.
    struct held_roles {
        std::string view;                 // like projects::member_roles
        std::string within;               // the entity the roles are held in, like project
        std::string field;                // the grant's field naming it, like project
        std::map<std::string, std::vector<std::string>> granting;  // command, like projects::issue::close, and the roles that grant it
        // For roles each project defines for itself: what the view's rows say each role
        // allows, like role.may, the namespace whose commands they allow, and the
        // commands anyone may run, which no role needs to allow.
        std::string allows;
        std::string ns;
        std::set<std::string> open;
        std::string record;  // the entity a role is a record of, like role
    };

    // The roles held within something, each granted by an entity, and the source of
    // the views that say who holds them.
    struct role_views {
        std::vector<held_roles> held;
        std::vector<language::file> files;  // the views, as parsed .one
    };

    role_views roles_of(const std::vector<language::file>& files);

} // namespace one::generators
