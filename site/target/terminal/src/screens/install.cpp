// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from install.one by one. Do not edit.

#include "../app.hpp"
#include "../pages/install.hpp"

namespace app {

	// Each of install.one's screens, added to the site.
	void install_screens(site& s) {
		s.screens.push_back({"Install", "/install/:page?", "Install", [](page& p) {
			return items(
				pages(p, "/install", install_pages)
			);
		}});
	}

}  // namespace app
