// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// The clipboard, through the seat's data device: text copied is offered to every
// program as UTF-8 until another copies something, and text pasted is read from
// whoever offers it now. Without a compositor, as in tests, it's kept here.

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <cstring>

#include "app_internal.hpp"
#include "ui_internal.hpp"

namespace one::desktop {

	namespace {

		app::state* owning = nullptr;
		std::string kept_here;  // the clipboard when there's no compositor

		constexpr const char* utf8 = "text/plain;charset=utf-8";

		bool is_text(const char* mime) {
			return std::strcmp(mime, utf8) == 0 || std::strcmp(mime, "text/plain") == 0 || std::strcmp(mime, "UTF8_STRING") == 0 || std::strcmp(mime, "TEXT") == 0;
		}

		const wl_data_offer_listener offer_listener{
		    .offer =
		        [](void*, wl_data_offer* offer, const char* mime) {
			        auto& c = owning->input->clipboard;
			        if (offer == c.coming && is_text(mime) && (c.coming_type.empty() || std::strcmp(mime, utf8) == 0)) c.coming_type = mime;
		        },
		    .source_actions = [](void*, wl_data_offer*, std::uint32_t) {},
		    .action = [](void*, wl_data_offer*, std::uint32_t) {},
		};

		const wl_data_source_listener source_listener{
		    .target = [](void*, wl_data_source*, const char*) {},
		    .send =
		        [](void*, wl_data_source*, const char*, std::int32_t fd) {
			        owned_descriptor to{fd};
			        const std::string& text = owning->input->clipboard.copied;
			        for (std::size_t sent = 0; sent < text.size();) {
				        ssize_t wrote = write(to.get(), text.data() + sent, text.size() - sent);
				        if (wrote <= 0) break;
				        sent += static_cast<std::size_t>(wrote);
			        }
		        },
		    // Another program copied something: ours is no longer offered.
		    .cancelled =
		        [](void*, wl_data_source* source) {
			        auto& c = owning->input->clipboard;
			        if (c.source.get() == source) c.source.reset();
		        },
		    .dnd_drop_performed = [](void*, wl_data_source*) {},
		    .dnd_finished = [](void*, wl_data_source*) {},
		    .action = [](void*, wl_data_source*, std::uint32_t) {},
		};

		const wl_data_device_listener device_listener{
		    // An offer's types follow it; whether it's the clipboard's or a drag's comes after.
		    .data_offer =
		        [](void*, wl_data_device*, wl_data_offer* offer) {
			        auto& c = owning->input->clipboard;
			        c.coming = offer;
			        c.coming_type.clear();
			        wl_data_offer_add_listener(offer, &offer_listener, nullptr);
		        },
		    // Drags aren't taken yet; their offers are let go.
		    .enter = [](void*, wl_data_device*, std::uint32_t, wl_surface*, wl_fixed_t, wl_fixed_t, wl_data_offer* offer) {
			    if (offer) wl_data_offer_destroy(offer);
		    },
		    .leave = [](void*, wl_data_device*) {},
		    .motion = [](void*, wl_data_device*, std::uint32_t, wl_fixed_t, wl_fixed_t) {},
		    .drop = [](void*, wl_data_device*) {},
		    .selection =
		        [](void*, wl_data_device*, wl_data_offer* offer) {
			        auto& c = owning->input->clipboard;
			        c.offered.reset();
			        c.offered_type.clear();
			        if (!offer) return;
			        c.offered = offer;
			        if (offer == c.coming) c.offered_type = c.coming_type;
		        },
		};

	}  // namespace

	void connect_clipboard(app::state& a) {
		owning = &a;
		if (!a.data_devices || !a.input || !a.input->handle || a.input->clipboard.device) return;
		auto& c = a.input->clipboard;
		c.device = wl_data_device_manager_get_data_device(a.data_devices, a.input->handle);
		wl_data_device_add_listener(c.device, &device_listener, nullptr);
	}

	namespace detail {

		void copy_text(std::string text) {
			if (!owning || !owning->input || !owning->input->clipboard.device) {
				kept_here = std::move(text);
				return;
			}
			auto& s = *owning->input;
			auto& c = s.clipboard;
			c.copied = std::move(text);
			c.source = wl_data_device_manager_create_data_source(owning->data_devices);
			wl_data_source_add_listener(c.source, &source_listener, nullptr);
			wl_data_source_offer(c.source, utf8);
			wl_data_source_offer(c.source, "text/plain");
			wl_data_source_offer(c.source, "UTF8_STRING");
			wl_data_device_set_selection(c.device, c.source, s.input_serial);
		}

		std::optional<std::string> pasted_text() {
			if (!owning || !owning->input || !owning->input->clipboard.device) return kept_here;
			auto& c = owning->input->clipboard;
			if (c.source) return c.copied;  // ours, which we'd otherwise wait on ourselves to send
			if (!c.offered || c.offered_type.empty()) return std::nullopt;
			int ends[2];
			if (pipe2(ends, O_CLOEXEC) != 0) return std::nullopt;
			owned_descriptor reading{ends[0]};
			{
				owned_descriptor writing{ends[1]};
				wl_data_offer_receive(c.offered, c.offered_type.c_str(), writing.get());
			}
			wl_display_flush(owning->display);
			// The program offering it writes it now; a second without a word from it is enough.
			std::string out;
			char buffer[4096];
			for (;;) {
				pollfd ready{.fd = reading.get(), .events = POLLIN, .revents = 0};
				if (poll(&ready, 1, 1000) <= 0) break;
				ssize_t got = read(reading.get(), buffer, sizeof buffer);
				if (got <= 0) break;
				out.append(buffer, static_cast<std::size_t>(got));
			}
			return out;
		}

	}  // namespace detail

}  // namespace one::desktop
