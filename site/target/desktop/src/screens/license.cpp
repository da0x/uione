// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from license.one by one. Do not edit.

#include "../app.hpp"

namespace app {

	// Each of license.one's screens, added to the site.
	void license_screens(site& s) {
		s.screens.push_back({"License", "/license", "License", [](page& p) {
			return items(
				hero("uione is free software, under the GNU AGPL, version 3 only.", text({words("You may use it, study it, change it and share it. Whoever you share it with gets the same freedoms, and so do the people who use a changed version over a network.")})),
				section({"What it covers"}, text({words("All of uione: the compiler, the libraries apps are built on (@uione/react, @uione/radix, @uione/editor, @uione/compiler and the Go library one), the desktop and terminal toolkits, the infrastructure library, the tools, the examples and this site. Each file says so in its first lines, as SPDX-License-Identifier: AGPL-3.0-only.")})),
				section({"Version 3 only"}, text({words("It's version 3 of the license, not any later one, so what the license asks of you doesn't change unless uione's own license does.")})),
				section({"Read it in full"}, actions(link(p, "The license, as it's kept with uione's code", "https://github.com/da0x/uione/blob/main/LICENSE"), link(p, "The GNU Affero General Public License, at gnu.org", "https://www.gnu.org/licenses/agpl-3.0.html")))
			);
		}});
	}

}  // namespace app
