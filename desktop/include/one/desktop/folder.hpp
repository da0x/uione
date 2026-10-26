// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A file shown in the desktop's file manager, its folder open with it picked, through
// org.freedesktop.FileManager1, as the file managers people use offer it.

#pragma once

#include <expected>
#include <filesystem>
#include <string>

namespace one::desktop {

	// The file picked in its folder, or the folder alone opened where no file manager
	// answers; or why neither could be, in words a person can act on. It asks over the
	// session's message bus, so call it away from the window's thread.
	std::expected<void, std::string> show_in_folder(const std::filesystem::path& file);

}  // namespace one::desktop
