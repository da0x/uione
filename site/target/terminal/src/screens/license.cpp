// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from license.one by one. Do not edit.

#include "../app.hpp"
#include "../pages/license.hpp"

namespace app {

	// Each of license.one's screens, added to the site.
	void license_screens(site& s) {
		s.screens.push_back({"License", "/license", "License", [](page& p) {
			return items(
				subtitle(p, {words("uione is free software, licensed under the GNU Affero General Public License, version 3 only (AGPL-3.0-only): the compiler, the libraries apps are built on, the desktop and terminal toolkits, the tools, the examples and this site. You may use it, study it, change it and share it, and whoever you share it with gets the same freedoms.")}),
				pages(p, "/license", license_pages, true)
			);
		}});
	}

}  // namespace app
