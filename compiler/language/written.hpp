// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <string>
#include <variant>

#include "language/ast.hpp"

namespace one::language {

    // The written name of a value, like book.title or waitlist::signups.
    inline std::string text_of(const expression& e) {
        if (auto* name = std::get_if<name_expression>(&e.node)) return name->name.text();
        if (auto* member = std::get_if<member_expression>(&e.node)) return text_of(*member->object) + "." + member->member;
        return "";
    }

} // namespace one::language
