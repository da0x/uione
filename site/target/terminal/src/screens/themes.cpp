// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from themes.one by one. Do not edit.

#include "../app.hpp"

namespace app {

	// Each of themes.one's screens, added to the site.
	void themes_screens(site& s) {
		s.screens.push_back({"Theme generator", "/theme-generator", "Theme generator", [](page& p) {
			return items(
				subtitle(p, {words("Pick a scheme and make a theme. The whole site is drawn in it at once, light and dark, so you can see it before you keep it.")}),
				themes(p),
				section({"How a theme is made"}, text({words("A seed picks a hue and how vivid it is, and the scheme picks the hues around it: complementary, analogous, triadic, split complementary or monochrome. The page, its panels, text, lines and accent all come from them.")}), text({words("Danger, warning and success stay red, amber and green, so they mean what they always do, each leaning a little to the palette. Every color reads on what it sits on, at 4.5:1 or more, light and dark, as the compiler checks a theme, so the theme you copy is one a project can say as it is.")})),
				section({"Using it"}, text({words("Copy the theme and paste it into any .one file of your project, then name it in the project block, like theme triadic_48213.")}), text({words("The same seed and scheme make the same theme in the desktop and terminal apps, and on the command line: one theme --seed 48213 --scheme triadic.")}), link(p, "Learn the language", "/language"))
			);
		}});
	}

}  // namespace app
