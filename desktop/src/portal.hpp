// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A question put to the desktop's portals, org.freedesktop.portal.*, which answer on a
// request object once the person has: a screenshot taken, or files chosen.

#pragma once

#include <dbus/dbus.h>

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace one::desktop::portal {

	// What a portal answered: done, 0, cancelled by the person, 1, ended otherwise, 2, or
	// nothing in the time waited, 3; and the files its results name.
	struct answer {
		std::uint32_t response = 2;
		std::vector<std::filesystem::path> files = {};
	};

	// An option of a portal's: a name and a value of one type, in a variant.
	void option(DBusMessageIter& options, const char* name, int type, const void* value);

	// The portal's method asked, with the words it takes after its parent window, then its
	// options, a handle_token among them and the rest added by `more`; and its answer,
	// waited for up to `wait`. `asked` names what's asked for in the words of a failure,
	// like "a screenshot", and `portal` the portal, like "screenshot".
	std::expected<answer, std::string> ask(const char* interface, const char* method, const std::vector<std::string>& words, const std::function<void(DBusMessageIter& options)>& more,
	                                       std::chrono::seconds wait, const std::string& asked, const std::string& portal);

}  // namespace one::desktop::portal
