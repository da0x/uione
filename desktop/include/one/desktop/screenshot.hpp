// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A screenshot taken through the desktop's own screenshot portal,
// org.freedesktop.portal.Screenshot, so the desktop decides how it's taken and asks
// the person as it asks any app; and the app needs nothing of the compositor's.

#pragma once

#include <expected>
#include <filesystem>
#include <string>

namespace one::desktop {

	// The screen, or the part the person picks when `interactive`, saved where the
	// desktop keeps screenshots: the file, or why there's none, in words a person can
	// act on. It waits for the person, up to two minutes, so call it away from the
	// window's thread and hand the answer back, as runtime::post does.
	std::expected<std::filesystem::path, std::string> screenshot(bool interactive = false);

}  // namespace one::desktop
