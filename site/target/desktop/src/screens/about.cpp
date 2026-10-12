// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from about.one by one. Do not edit.

#include "../app.hpp"
#include "../pages/about.hpp"

namespace app {

	// Each of about.one's screens, added to the site.
	void about_screens(site& s) {
		s.screens.push_back({"About", "/about/:page?", "About", [](page& p) {
			return items(
				pages(p, "/about", about_pages)
			);
		}});
	}

}  // namespace app
