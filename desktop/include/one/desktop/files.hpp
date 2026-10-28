// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Files chosen in the desktop's own file chooser, through
// org.freedesktop.portal.FileChooser, so it's the dialog the person knows from every
// other app, and the app needs nothing of the toolkit the desktop is made with.

#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace one::desktop {

	struct choosing {
		std::string title;     // what the dialog is titled, like "Add pictures"
		bool pictures = false;  // only pictures shown
		bool several = false;   // more than one may be chosen
	};

	// The files chosen, none when the person cancels, or why it couldn't ask, in words a
	// person can act on. It waits for the person, so call it away from the window's
	// thread and hand the answer back, as runtime::post does.
	std::expected<std::vector<std::filesystem::path>, std::string> chosen_files(const choosing& what);

}  // namespace one::desktop
