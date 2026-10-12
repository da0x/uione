// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// State, as one::screens has it for every toolkit: a value whose readers are known,
// so a window's view is built again when one it read changes.

#pragma once

#include <one/screens/state.hpp>

namespace one::desktop {

	using screens::state;

	namespace detail {
		using screens::detail::current_reader;
		using screens::detail::reader;
		using screens::detail::reading;
	}  // namespace detail

}  // namespace one::desktop
