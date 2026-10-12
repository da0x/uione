// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from language.one by one. Do not edit.

#include "../app.hpp"
#include "../pages/language.hpp"

namespace app {

	// Each of language.one's screens, added to the site.
	void language_screens(site& s) {
		s.screens.push_back({"Language", "/language/:page?", "Language", [](page& p) {
			return items(
				pages(p, "/language", language_pages)
			);
		}});
	}

}  // namespace app
