// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from site.one by one. Do not edit.

#pragma once

#include <one/desktop/runtime/components.hpp>
#include <one/desktop/runtime/markdown.hpp>

namespace app {

	using namespace one::desktop::runtime;
	using view = one::desktop::view;

	// What the app was built from, which a footer can show: the uione release, and the
	// commit and repository its build found.
	inline const std::string build_version = "0.15.2";
	inline const std::string build_commit = UIONE_COMMIT;
	inline const std::string build_repository = UIONE_REPOSITORY;

	// The badges beside the app's name, and its footer.
	void site_parts(site& s);
	void about_screens(site& s);
	void home_screens(site& s);
	void install_screens(site& s);
	void language_screens(site& s);
	void license_screens(site& s);
	void mission_screens(site& s);
	void releases_screens(site& s);
	void themes_screens(site& s);

}  // namespace app
