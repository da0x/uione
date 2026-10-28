// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Something said by the desktop's own notifications, org.freedesktop.Notifications, as
// any app's are: a title, what's under it, and buttons, the one pressed told back, so
// it's said the same way whichever desktop it is, and the app needs nothing of it.

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace one::desktop {

	struct notice_action {
		std::string key, label;  // what's told back when it's pressed, and its words
	};

	// Says it, and tells `chosen` the key of the button pressed, on another thread, or
	// never, when it's put away or there's no desktop to say it. It returns at once.
	void notify(const std::string& app, const std::string& title, const std::string& body, std::vector<notice_action> actions,
	            std::function<void(const std::string& key)> chosen);

}  // namespace one::desktop
