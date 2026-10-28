// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "one/desktop/screenshot.hpp"

#include "portal.hpp"

namespace one::desktop {

	std::expected<std::filesystem::path, std::string> screenshot(bool interactive) {
		dbus_bool_t asks = interactive;
		auto said = portal::ask(
		    "org.freedesktop.portal.Screenshot", "Screenshot", {}, [&](DBusMessageIter& options) { portal::option(options, "interactive", DBUS_TYPE_BOOLEAN, &asks); },
		    std::chrono::minutes(2), "a screenshot", "screenshot");
		if (!said) return std::unexpected(said.error());
		if (said->response == 1) return std::unexpected(std::string("The screenshot was cancelled."));
		if (said->response == 3) return std::unexpected(std::string("No screenshot came within two minutes."));
		if (said->response != 0) return std::unexpected(std::string("The desktop couldn't take a screenshot."));
		if (said->files.empty()) return std::unexpected(std::string("The desktop took a screenshot but didn't say where it is."));
		return said->files.front();
	}

}  // namespace one::desktop
