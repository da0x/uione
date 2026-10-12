// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// Markdown on a desktop app's screens, drawn from one::markdown's tree in the
// theme's styles. It's a library of its own, one_desktop_markdown, because
// one::markdown is GPL-3.0: an app whose screens show Markdown links it, and one
// whose screens don't, needn't.

#pragma once

#include <string>

#include "one/desktop/runtime/runtime.hpp"

namespace one::desktop::runtime {

	// Markdown as views.
	desktop::view markdown_view(const std::string& source);
	// A view's field of Markdown, like an issue's body, with what it says while
	// there's nothing.
	desktop::view markdown(page& p, state<view_state> from, std::string field, std::string empty = "");

}  // namespace one::desktop::runtime
