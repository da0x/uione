// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The clipboard, through the seat's data device: text copied is offered to every
// program as UTF-8 until another copies something, and text, a picture or files
// pasted are read from whoever offers them now. Without a compositor, as in tests,
// text is kept here. Files dragged from another program, as from a file manager, are
// taken by the file target they're let go over.

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <cstring>
#include <sstream>

#include "app_internal.hpp"
#include "ui_internal.hpp"

namespace one::desktop {

	namespace {

		app::state* owning = nullptr;
		std::string kept_here;  // the clipboard when there's no compositor

		constexpr const char* utf8 = "text/plain;charset=utf-8";

		constexpr const char* files = "text/uri-list";

		bool is_text(const char* mime) {
			return std::strcmp(mime, utf8) == 0 || std::strcmp(mime, "text/plain") == 0 || std::strcmp(mime, "UTF8_STRING") == 0 || std::strcmp(mime, "TEXT") == 0;
		}

		// The kinds of picture that can be read, the best first.
		constexpr const char* pictures[] = {"image/png", "image/jpeg", "image/gif", "image/bmp", "image/x-tga"};
		int picture_rank(std::string_view mime) {
			for (int i = 0; i < static_cast<int>(std::size(pictures)); ++i)
				if (mime == pictures[i]) return i;
			return -1;
		}

		const wl_data_offer_listener offer_listener{
		    .offer =
		        [](void*, wl_data_offer* offer, const char* mime) {
			        auto& c = owning->input->clipboard;
			        if (offer != c.coming) return;
			        if (is_text(mime) && (c.coming_type.empty() || std::strcmp(mime, utf8) == 0)) c.coming_type = mime;
			        if (int rank = picture_rank(mime); rank >= 0 && (c.coming_picture.empty() || rank < picture_rank(c.coming_picture))) c.coming_picture = mime;
			        if (std::strcmp(mime, files) == 0) c.coming_files = true;
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

		// What an offer holds in one of its kinds, read through a pipe as the program
		// offering it writes it; a second without a word from it is enough.
		std::string receive(wl_data_offer* offer, const std::string& mime) {
			int ends[2];
			if (pipe2(ends, O_CLOEXEC) != 0) return "";
			owned_descriptor reading{ends[0]};
			{
				owned_descriptor writing{ends[1]};
				wl_data_offer_receive(offer, mime.c_str(), writing.get());
			}
			wl_display_flush(owning->display);
			std::string out;
			char buffer[65536];
			for (;;) {
				pollfd ready{.fd = reading.get(), .events = POLLIN, .revents = 0};
				if (poll(&ready, 1, 1000) <= 0) break;
				ssize_t got = read(reading.get(), buffer, sizeof buffer);
				if (got <= 0) break;
				out.append(buffer, static_cast<std::size_t>(got));
			}
			return out;
		}

		void redraw_dragged_over() {
			auto& c = owning->input->clipboard;
			if (c.dragged_over) c.dragged_over->dirty = true;
		}

		void stop_dragging() {
			auto& c = owning->input->clipboard;
			redraw_dragged_over();
			c.dragged.reset();
			c.dragged_over = nullptr;
			c.dragged_files = false;
			c.dragged_picture.clear();
			detail::files_carried().reset();
		}

		const wl_data_device_listener device_listener{
		    // An offer's types follow it; whether it's the clipboard's or a drag's comes after.
		    .data_offer =
		        [](void*, wl_data_device*, wl_data_offer* offer) {
			        auto& c = owning->input->clipboard;
			        c.coming = offer;
			        c.coming_type.clear();
			        c.coming_picture.clear();
			        c.coming_files = false;
			        wl_data_offer_add_listener(offer, &offer_listener, nullptr);
		        },
		    // Something dragged over a window: files or a picture are taken, as a copy, and
		    // anything else is let go.
		    .enter =
		        [](void*, wl_data_device*, std::uint32_t serial, wl_surface* surface, wl_fixed_t x, wl_fixed_t y, wl_data_offer* offer) {
			        auto& c = owning->input->clipboard;
			        stop_dragging();
			        if (!offer) return;
			        bool ours = offer == c.coming;
			        if (!ours || (!c.coming_files && c.coming_picture.empty())) {
				        wl_data_offer_destroy(offer);
				        return;
			        }
			        c.dragged = offer;
			        for (auto& w : owning->windows)
				        if (w->internals().surface.get() == surface) c.dragged_over = &w->internals();
			        c.dragged_files = c.coming_files;
			        c.dragged_picture = c.coming_picture;
			        c.dragged_at = {static_cast<float>(wl_fixed_to_double(x)), static_cast<float>(wl_fixed_to_double(y))};
			        detail::files_carried() = c.dragged_at;
			        wl_data_offer_accept(offer, serial, c.dragged_files ? files : c.dragged_picture.c_str());
			        if (wl_data_offer_get_version(offer) >= WL_DATA_OFFER_SET_ACTIONS_SINCE_VERSION)
				        wl_data_offer_set_actions(offer, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
			        redraw_dragged_over();
		        },
		    .leave = [](void*, wl_data_device*) { stop_dragging(); },
		    .motion =
		        [](void*, wl_data_device*, std::uint32_t, wl_fixed_t x, wl_fixed_t y) {
			        auto& c = owning->input->clipboard;
			        if (!c.dragged) return;
			        c.dragged_at = {static_cast<float>(wl_fixed_to_double(x)), static_cast<float>(wl_fixed_to_double(y))};
			        detail::files_carried() = c.dragged_at;
			        redraw_dragged_over();
		        },
		    // Let go: files on this machine are read, or else the picture itself, and given
		    // to the file target they're over.
		    .drop =
		        [](void*, wl_data_device*) {
			        auto& c = owning->input->clipboard;
			        if (!c.dragged || !c.dragged_over) return stop_dragging();
			        dropped what;
			        if (c.dragged_files) {
				        std::string listed = receive(c.dragged.get(), files);
				        what.files = detail::paths_of(listed);
				        if (what.files.empty()) what.elsewhere = detail::elsewhere_of(listed);
			        }
			        if (what.files.empty() && !c.dragged_picture.empty()) {
				        std::string bytes = receive(c.dragged.get(), c.dragged_picture);
				        what.picture.assign(bytes.begin(), bytes.end());
			        }
			        if (wl_data_offer_get_version(c.dragged.get()) >= WL_DATA_OFFER_FINISH_SINCE_VERSION) wl_data_offer_finish(c.dragged.get());
			        window::state* over = c.dragged_over;
			        point at = c.dragged_at;
			        stop_dragging();
			        bool any = !what.files.empty() || !what.picture.empty() || !what.elsewhere.empty();
			        if (any && over->root) detail::drop_files(*over->root, at, std::move(what));
		        },
		    .selection =
		        [](void*, wl_data_device*, wl_data_offer* offer) {
			        auto& c = owning->input->clipboard;
			        c.offered.reset();
			        c.offered_type.clear();
			        c.offered_picture.clear();
			        c.offered_files = false;
			        if (!offer) return;
			        c.offered = offer;
			        if (offer == c.coming) {
				        c.offered_type = c.coming_type;
				        c.offered_picture = c.coming_picture;
				        c.offered_files = c.coming_files;
			        }
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

	void copy_text(std::string text) { detail::copy_text(std::move(text)); }
	std::optional<std::string> clipboard_text() { return detail::pasted_text(); }

	std::optional<std::vector<std::uint8_t>> clipboard_picture() {
		if (!owning || !owning->input || !owning->input->clipboard.device) return std::nullopt;
		auto& c = owning->input->clipboard;
		if (c.source || !c.offered || c.offered_picture.empty()) return std::nullopt;
		std::string bytes = receive(c.offered.get(), c.offered_picture);
		if (bytes.empty()) return std::nullopt;
		return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
	}

	std::vector<std::filesystem::path> clipboard_files() {
		if (!owning || !owning->input || !owning->input->clipboard.device) return {};
		auto& c = owning->input->clipboard;
		if (c.source || !c.offered || !c.offered_files) return {};
		return detail::paths_of(receive(c.offered.get(), files));
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
			return receive(c.offered.get(), c.offered_type);
		}

		std::vector<std::string> elsewhere_of(const std::string& uri_list) {
			std::vector<std::string> out;
			std::istringstream lines(uri_list);
			std::string line;
			while (std::getline(lines, line)) {
				if (!line.empty() && line.back() == '\r') line.pop_back();
				if (!line.empty() && line.front() != '#' && !line.starts_with("file://")) out.push_back(line);
			}
			return out;
		}

		std::vector<std::filesystem::path> paths_of(const std::string& uri_list) {
			std::vector<std::filesystem::path> out;
			std::istringstream lines(uri_list);
			std::string line;
			while (std::getline(lines, line)) {
				if (!line.empty() && line.back() == '\r') line.pop_back();
				if (line.empty() || line.front() == '#' || !line.starts_with("file://")) continue;
				std::string rest = line.substr(7);
				// file://host/path names a path on that host; this one's is empty or localhost.
				if (!rest.starts_with("/")) {
					auto slash = rest.find('/');
					if (slash == std::string::npos || rest.substr(0, slash) != "localhost") continue;
					rest = rest.substr(slash);
				}
				std::string decoded;
				for (std::size_t i = 0; i < rest.size(); ++i) {
					if (rest[i] == '%' && i + 2 < rest.size() && std::isxdigit(static_cast<unsigned char>(rest[i + 1])) && std::isxdigit(static_cast<unsigned char>(rest[i + 2]))) {
						decoded += static_cast<char>(std::stoi(rest.substr(i + 1, 2), nullptr, 16));
						i += 2;
					} else {
						decoded += rest[i];
					}
				}
				out.emplace_back(decoded);
			}
			return out;
		}

	}  // namespace detail

}  // namespace one::desktop
