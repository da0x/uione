// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from releases.one by one. Do not edit.

#include "../app.hpp"
#include "../pages/releases.hpp"

namespace app {

	// Each of releases.one's screens, added to the site.
	void releases_screens(site& s) {
		s.screens.push_back({"Releases", "/releases/:page?", "Releases", [](page& p) {
			return items(
				pages(p, "/releases", releases_pages)
			);
		}});
	}

}  // namespace app
