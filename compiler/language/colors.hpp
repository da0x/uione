// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

// Colors as a theme writes them, as one::screens works them out for every target, so
// what the compiler checks is what the apps draw with.

#include <one/screens/colors.hpp>

namespace one::language {
    namespace colors = one::screens::colors;
}
