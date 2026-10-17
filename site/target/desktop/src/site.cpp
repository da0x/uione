// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from site.one by one. Do not edit.

#include "app.hpp"

namespace app {

	void site_parts([[maybe_unused]] site& s) {
		s.footer = [](page& p) {
			return actions(
				text({words("© "), words(this_year()), words(" [Daher Alfawares](https://www.linkedin.com/in/daheralfawares)")}),
				actions(link(p, said({words("uione "), words(build_version)}), "https://github.com/da0x/uione/releases/tag/v" + build_version), build_commit.empty() || build_repository.empty() ? view() : link(p, said({words(build_commit)}), build_repository + "/commit/" + build_commit), link(p, "AGPL-3.0-only", "/license"), link(p, "Donate", "https://opencollective.com/uione"))
			);
		};
	}

}  // namespace app
