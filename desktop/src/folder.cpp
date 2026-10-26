// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "one/desktop/folder.hpp"

#include <dbus/dbus.h>
#include <spawn.h>
#include <sys/wait.h>

#include <cctype>
#include <memory>

extern char** environ;

namespace one::desktop {

	namespace {

		struct connection_closed {
			void operator()(DBusConnection* c) const { dbus_connection_unref(c); }
		};
		struct message_freed {
			void operator()(DBusMessage* m) const { dbus_message_unref(m); }
		};

		// A path as a file:// URI, what isn't plainly safe written as %xx.
		std::string uri_of(const std::filesystem::path& file) {
			static const char* hex = "0123456789ABCDEF";
			std::string out = "file://";
			for (unsigned char c : file.string()) {
				if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') {
					out += static_cast<char>(c);
				} else {
					out += '%';
					out += hex[c >> 4];
					out += hex[c & 15];
				}
			}
			return out;
		}

		// The file manager asked to show it, picked in its folder.
		bool asked_file_manager(const std::filesystem::path& file) {
			DBusError error;
			dbus_error_init(&error);
			std::unique_ptr<DBusConnection, connection_closed> bus{dbus_bus_get(DBUS_BUS_SESSION, &error)};
			if (!bus) {
				dbus_error_free(&error);
				return false;
			}
			std::unique_ptr<DBusMessage, message_freed> call{dbus_message_new_method_call("org.freedesktop.FileManager1", "/org/freedesktop/FileManager1", "org.freedesktop.FileManager1", "ShowItems")};
			DBusMessageIter arguments, uris;
			dbus_message_iter_init_append(call.get(), &arguments);
			dbus_message_iter_open_container(&arguments, DBUS_TYPE_ARRAY, "s", &uris);
			std::string uri = uri_of(file);
			const char* item = uri.c_str();
			dbus_message_iter_append_basic(&uris, DBUS_TYPE_STRING, &item);
			dbus_message_iter_close_container(&arguments, &uris);
			const char* startup = "";
			dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &startup);
			std::unique_ptr<DBusMessage, message_freed> reply{dbus_connection_send_with_reply_and_block(bus.get(), call.get(), 5000, &error)};
			dbus_error_free(&error);
			return reply != nullptr;
		}

	}  // namespace

	std::expected<void, std::string> show_in_folder(const std::filesystem::path& file) {
		if (asked_file_manager(file)) return {};
		// Where no file manager answers, the folder opened as the desktop opens folders.
		std::string folder = file.parent_path().string();
		const char* arguments[] = {"xdg-open", folder.c_str(), nullptr};
		pid_t child = 0;
		if (posix_spawnp(&child, "xdg-open", nullptr, nullptr, const_cast<char* const*>(arguments), environ) != 0) {
			return std::unexpected("There's no file manager to show it in. It's in " + folder + ".");
		}
		int status = 0;
		waitpid(child, &status, 0);
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return std::unexpected("The folder couldn't be opened. It's in " + folder + ".");
		return {};
	}

}  // namespace one::desktop
