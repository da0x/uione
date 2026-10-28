// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "one/desktop/notices.hpp"

#include <dbus/dbus.h>

#include <chrono>
#include <cstring>
#include <memory>
#include <thread>

namespace one::desktop {

	namespace {

		struct connection_closed {
			void operator()(DBusConnection* c) const {
				dbus_connection_close(c);
				dbus_connection_unref(c);
			}
		};
		struct message_freed {
			void operator()(DBusMessage* m) const { dbus_message_unref(m); }
		};
		using connection = std::unique_ptr<DBusConnection, connection_closed>;
		using message = std::unique_ptr<DBusMessage, message_freed>;

		// Said, and waited on until a button's pressed or it's put away, up to a day.
		void said(const std::string& app, const std::string& title, const std::string& body, const std::vector<notice_action>& actions,
		          const std::function<void(const std::string&)>& chosen) {
			DBusError error;
			dbus_error_init(&error);
			connection bus{dbus_bus_get_private(DBUS_BUS_SESSION, &error)};
			if (!bus) {
				dbus_error_free(&error);
				return;
			}
			dbus_connection_set_exit_on_disconnect(bus.get(), false);
			// Listened for before it's said, so the answer can't be missed.
			dbus_bus_add_match(bus.get(), "type='signal',interface='org.freedesktop.Notifications'", &error);
			if (dbus_error_is_set(&error)) {
				dbus_error_free(&error);
				return;
			}
			message call{dbus_message_new_method_call("org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "Notify")};
			DBusMessageIter arguments, list, hints;
			dbus_message_iter_init_append(call.get(), &arguments);
			const char* name = app.c_str();
			dbus_uint32_t replaces = 0;
			const char* icon = "";
			const char* summary = title.c_str();
			const char* text = body.c_str();
			dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &name);
			dbus_message_iter_append_basic(&arguments, DBUS_TYPE_UINT32, &replaces);
			dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &icon);
			dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &summary);
			dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &text);
			// Its buttons, each a key and its words, one after the other.
			dbus_message_iter_open_container(&arguments, DBUS_TYPE_ARRAY, "s", &list);
			for (const auto& a : actions) {
				const char* key = a.key.c_str();
				const char* label = a.label.c_str();
				dbus_message_iter_append_basic(&list, DBUS_TYPE_STRING, &key);
				dbus_message_iter_append_basic(&list, DBUS_TYPE_STRING, &label);
			}
			dbus_message_iter_close_container(&arguments, &list);
			dbus_message_iter_open_container(&arguments, DBUS_TYPE_ARRAY, "{sv}", &hints);
			dbus_message_iter_close_container(&arguments, &hints);
			dbus_int32_t expires = 0;  // until it's put away, as a reminder should be
			dbus_message_iter_append_basic(&arguments, DBUS_TYPE_INT32, &expires);
			message reply{dbus_connection_send_with_reply_and_block(bus.get(), call.get(), 5000, &error)};
			if (!reply) {
				dbus_error_free(&error);
				return;
			}
			dbus_uint32_t id = 0;
			if (!dbus_message_get_args(reply.get(), &error, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID)) {
				dbus_error_free(&error);
				return;
			}
			auto until = std::chrono::steady_clock::now() + std::chrono::hours(24);
			while (std::chrono::steady_clock::now() < until) {
				if (!dbus_connection_read_write(bus.get(), 1000)) return;
				while (message m{dbus_connection_pop_message(bus.get())}) {
					dbus_uint32_t which = 0;
					if (dbus_message_is_signal(m.get(), "org.freedesktop.Notifications", "ActionInvoked")) {
						const char* key = nullptr;
						if (!dbus_message_get_args(m.get(), nullptr, DBUS_TYPE_UINT32, &which, DBUS_TYPE_STRING, &key, DBUS_TYPE_INVALID) || which != id) continue;
						if (key && chosen) chosen(key);
						return;
					}
					if (dbus_message_is_signal(m.get(), "org.freedesktop.Notifications", "NotificationClosed")) {
						if (dbus_message_get_args(m.get(), nullptr, DBUS_TYPE_UINT32, &which, DBUS_TYPE_INVALID) && which == id) return;
					}
				}
			}
		}

	}  // namespace

	void notify(const std::string& app, const std::string& title, const std::string& body, std::vector<notice_action> actions,
	            std::function<void(const std::string& key)> chosen) {
		std::thread([app, title, body, actions = std::move(actions), chosen = std::move(chosen)] { said(app, title, body, actions, chosen); }).detach();
	}

}  // namespace one::desktop
