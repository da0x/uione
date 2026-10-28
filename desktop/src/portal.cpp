// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "portal.hpp"

#include <cstring>
#include <format>
#include <memory>
#include <random>

namespace one::desktop::portal {

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

		// The files among a response's results: its uri, or its uris.
		std::vector<std::filesystem::path> files_of(DBusMessageIter& results) {
			std::vector<std::filesystem::path> out;
			DBusMessageIter entry;
			dbus_message_iter_recurse(&results, &entry);
			while (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_DICT_ENTRY) {
				DBusMessageIter pair, variant;
				dbus_message_iter_recurse(&entry, &pair);
				const char* name = nullptr;
				dbus_message_iter_get_basic(&pair, &name);
				dbus_message_iter_next(&pair);
				dbus_message_iter_recurse(&pair, &variant);
				const char* uri = nullptr;
				if (name && std::strcmp(name, "uri") == 0 && dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRING) {
					dbus_message_iter_get_basic(&variant, &uri);
					if (uri && *uri) out.push_back(path_of(uri));
				} else if (name && std::strcmp(name, "uris") == 0 && dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_ARRAY) {
					DBusMessageIter each;
					dbus_message_iter_recurse(&variant, &each);
					while (dbus_message_iter_get_arg_type(&each) == DBUS_TYPE_STRING) {
						dbus_message_iter_get_basic(&each, &uri);
						if (uri && *uri) out.push_back(path_of(uri));
						dbus_message_iter_next(&each);
					}
				}
				dbus_message_iter_next(&entry);
			}
			return out;
		}

	}  // namespace

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

	std::expected<answer, std::string> ask(const char* interface, const char* method, const std::vector<std::string>& words, const std::function<void(DBusMessageIter& options)>& more,
	                                       std::chrono::seconds wait, const std::string& asked, const std::string& portal) {
		DBusError error;
		dbus_error_init(&error);
		connection bus{dbus_bus_get_private(DBUS_BUS_SESSION, &error)};
		if (!bus) {
			dbus_error_free(&error);
			return std::unexpected("The desktop's message bus isn't running, so there's no way to ask for " + asked + ".");
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
			return std::unexpected("The desktop's " + portal + " portal couldn't be listened to.");
		}

		message call{dbus_message_new_method_call("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", interface, method)};
		DBusMessageIter arguments, options;
		dbus_message_iter_init_append(call.get(), &arguments);
		const char* parent = "";
		dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &parent);
		for (const auto& w : words) {
			const char* said = w.c_str();
			dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &said);
		}
		dbus_message_iter_open_container(&arguments, DBUS_TYPE_ARRAY, "{sv}", &options);
		const char* handle = token.c_str();
		option(options, "handle_token", DBUS_TYPE_STRING, &handle);
		if (more) more(options);
		dbus_message_iter_close_container(&arguments, &options);
		message reply{dbus_connection_send_with_reply_and_block(bus.get(), call.get(), 10000, &error)};
		if (!reply) {
			std::string why = error.message ? error.message : "";
			dbus_error_free(&error);
			return std::unexpected("The desktop has no " + portal + " portal to ask" + (why.empty() ? std::string(".") : ": " + why));
		}

		auto until = std::chrono::steady_clock::now() + wait;
		while (std::chrono::steady_clock::now() < until) {
			if (!dbus_connection_read_write(bus.get(), 500)) return std::unexpected(std::string("The desktop's message bus went away."));
			while (message m{dbus_connection_pop_message(bus.get())}) {
				if (!dbus_message_is_signal(m.get(), "org.freedesktop.portal.Request", "Response") || request != dbus_message_get_path(m.get())) continue;
				answer out;
				DBusMessageIter said;
				if (dbus_message_iter_init(m.get(), &said) && dbus_message_iter_get_arg_type(&said) == DBUS_TYPE_UINT32) {
					dbus_message_iter_get_basic(&said, &out.response);
					dbus_message_iter_next(&said);
				}
				if (out.response == 0 && dbus_message_iter_get_arg_type(&said) == DBUS_TYPE_ARRAY) out.files = files_of(said);
				return out;
			}
		}
		return answer{.response = 3};
	}

}  // namespace one::desktop::portal
