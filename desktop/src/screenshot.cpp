// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "one/desktop/screenshot.hpp"

#include <dbus/dbus.h>

#include <chrono>
#include <cstring>
#include <format>
#include <memory>
#include <random>

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

		// A file:// URI as the path it names, its %xx escapes read.
		std::filesystem::path path_of(std::string_view uri) {
			if (uri.starts_with("file://")) uri.remove_prefix(7);
			std::string out;
			for (std::size_t i = 0; i < uri.size(); ++i) {
				if (uri[i] == '%' && i + 2 < uri.size()) {
					out += static_cast<char>(std::stoi(std::string(uri.substr(i + 1, 2)), nullptr, 16));
					i += 2;
				} else {
					out += uri[i];
				}
			}
			return out;
		}

		// An option of the portal's: a name and a value of one type, in a variant.
		void option(DBusMessageIter& options, const char* name, int type, const void* value) {
			DBusMessageIter entry, variant;
			const char signature[2] = {static_cast<char>(type), 0};
			dbus_message_iter_open_container(&options, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
			dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &name);
			dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, signature, &variant);
			dbus_message_iter_append_basic(&variant, type, value);
			dbus_message_iter_close_container(&entry, &variant);
			dbus_message_iter_close_container(&options, &entry);
		}

		// The uri among a response's results, if it has one.
		std::string uri_of(DBusMessageIter& results) {
			DBusMessageIter entry;
			dbus_message_iter_recurse(&results, &entry);
			while (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_DICT_ENTRY) {
				DBusMessageIter pair, variant;
				dbus_message_iter_recurse(&entry, &pair);
				const char* name = nullptr;
				dbus_message_iter_get_basic(&pair, &name);
				dbus_message_iter_next(&pair);
				dbus_message_iter_recurse(&pair, &variant);
				if (name && std::strcmp(name, "uri") == 0 && dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRING) {
					const char* uri = nullptr;
					dbus_message_iter_get_basic(&variant, &uri);
					return uri ? uri : "";
				}
				dbus_message_iter_next(&entry);
			}
			return "";
		}

	}  // namespace

	std::expected<std::filesystem::path, std::string> screenshot(bool interactive) {
		DBusError error;
		dbus_error_init(&error);
		connection bus{dbus_bus_get_private(DBUS_BUS_SESSION, &error)};
		if (!bus) {
			dbus_error_free(&error);
			return std::unexpected(std::string("The desktop's message bus isn't running, so there's no way to ask for a screenshot."));
		}
		dbus_connection_set_exit_on_disconnect(bus.get(), false);

		// The portal answers on a request object it names from this connection and a
		// token chosen here, listened to before it's asked, so the answer can't be missed.
		std::string token = std::format("one{}", std::random_device{}() % 1000000000);
		std::string sender = dbus_bus_get_unique_name(bus.get());
		std::string escaped;
		for (char c : sender.substr(1)) escaped += c == '.' ? '_' : c;
		std::string request = "/org/freedesktop/portal/desktop/request/" + escaped + "/" + token;
		std::string rule = "type='signal',interface='org.freedesktop.portal.Request',member='Response',path='" + request + "'";
		dbus_bus_add_match(bus.get(), rule.c_str(), &error);
		if (dbus_error_is_set(&error)) {
			dbus_error_free(&error);
			return std::unexpected(std::string("The desktop's screenshot portal couldn't be listened to."));
		}

		message call{dbus_message_new_method_call("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.portal.Screenshot", "Screenshot")};
		DBusMessageIter arguments, options;
		dbus_message_iter_init_append(call.get(), &arguments);
		const char* parent = "";
		dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &parent);
		dbus_message_iter_open_container(&arguments, DBUS_TYPE_ARRAY, "{sv}", &options);
		const char* handle = token.c_str();
		dbus_bool_t asks = interactive;
		option(options, "handle_token", DBUS_TYPE_STRING, &handle);
		option(options, "interactive", DBUS_TYPE_BOOLEAN, &asks);
		dbus_message_iter_close_container(&arguments, &options);
		message reply{dbus_connection_send_with_reply_and_block(bus.get(), call.get(), 10000, &error)};
		if (!reply) {
			std::string why = error.message ? error.message : "";
			dbus_error_free(&error);
			return std::unexpected("The desktop has no screenshot portal to ask" + (why.empty() ? std::string(".") : ": " + why));
		}

		auto until = std::chrono::steady_clock::now() + std::chrono::minutes(2);
		while (std::chrono::steady_clock::now() < until) {
			if (!dbus_connection_read_write(bus.get(), 500)) return std::unexpected(std::string("The desktop's message bus went away."));
			while (message m{dbus_connection_pop_message(bus.get())}) {
				if (!dbus_message_is_signal(m.get(), "org.freedesktop.portal.Request", "Response") || request != dbus_message_get_path(m.get())) continue;
				DBusMessageIter answer;
				dbus_uint32_t response = 2;
				if (dbus_message_iter_init(m.get(), &answer) && dbus_message_iter_get_arg_type(&answer) == DBUS_TYPE_UINT32) {
					dbus_message_iter_get_basic(&answer, &response);
					dbus_message_iter_next(&answer);
				}
				if (response == 1) return std::unexpected(std::string("The screenshot was cancelled."));
				if (response != 0) return std::unexpected(std::string("The desktop couldn't take a screenshot."));
				std::string uri = dbus_message_iter_get_arg_type(&answer) == DBUS_TYPE_ARRAY ? uri_of(answer) : "";
				if (uri.empty()) return std::unexpected(std::string("The desktop took a screenshot but didn't say where it is."));
				return path_of(uri);
			}
		}
		return std::unexpected(std::string("No screenshot came within two minutes."));
	}

}  // namespace one::desktop
