// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "one/desktop/files.hpp"

#include "portal.hpp"

namespace one::desktop {

	std::expected<std::vector<std::filesystem::path>, std::string> chosen_files(const choosing& what) {
		auto said = portal::ask(
		    "org.freedesktop.portal.FileChooser", "OpenFile", {what.title},
		    [&](DBusMessageIter& options) {
			    dbus_bool_t several = what.several, modal = true;
			    portal::option(options, "multiple", DBUS_TYPE_BOOLEAN, &several);
			    portal::option(options, "modal", DBUS_TYPE_BOOLEAN, &modal);
			    if (!what.pictures) return;
			    // filters, a(sa(us)): one named Pictures, of every image type, kind 1.
			    DBusMessageIter entry, variant, filters, filter, rules, rule;
			    const char* name = "filters";
			    const char* shown = "Pictures";
			    const char* type = "image/*";
			    dbus_uint32_t by_type = 1;
			    dbus_message_iter_open_container(&options, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
			    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &name);
			    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "a(sa(us))", &variant);
			    dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "(sa(us))", &filters);
			    dbus_message_iter_open_container(&filters, DBUS_TYPE_STRUCT, nullptr, &filter);
			    dbus_message_iter_append_basic(&filter, DBUS_TYPE_STRING, &shown);
			    dbus_message_iter_open_container(&filter, DBUS_TYPE_ARRAY, "(us)", &rules);
			    dbus_message_iter_open_container(&rules, DBUS_TYPE_STRUCT, nullptr, &rule);
			    dbus_message_iter_append_basic(&rule, DBUS_TYPE_UINT32, &by_type);
			    dbus_message_iter_append_basic(&rule, DBUS_TYPE_STRING, &type);
			    dbus_message_iter_close_container(&rules, &rule);
			    dbus_message_iter_close_container(&filter, &rules);
			    dbus_message_iter_close_container(&filters, &filter);
			    dbus_message_iter_close_container(&variant, &filters);
			    dbus_message_iter_close_container(&entry, &variant);
			    dbus_message_iter_close_container(&options, &entry);
		    },
		    std::chrono::hours(1), "files", "file chooser");
		if (!said) return std::unexpected(said.error());
		if (said->response == 1 || said->response == 3) return std::vector<std::filesystem::path>{};
		if (said->response != 0) return std::unexpected(std::string("The desktop's file chooser couldn't open."));
		return said->files;
	}

}  // namespace one::desktop
