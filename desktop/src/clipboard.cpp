// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The clipboard, through the seat's data device: text copied is offered to every
// program as UTF-8 until another copies something, and text, a picture or files
// pasted are read from whoever offers them now. Without a compositor, as in tests,
// text is kept here. Files dragged from another program, as from a file manager, are
// taken by the file target they're let go over, and a file is carried out to another
// program the same way.

#include <fcntl.h>
#include <sys/mman.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

#include "app_internal.hpp"
#include "ui_internal.hpp"

namespace one::desktop {

	namespace {

		app::state* owning = nullptr;
		std::string kept_here;  // the clipboard when there's no compositor
		std::optional<std::vector<std::uint8_t>> picture_kept_here;  // and a picture on it

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

		// The kind of picture a file holds, from its first bytes, or none.
		std::string picture_kind(const std::filesystem::path& file) {
			unsigned char b[12]{};
			std::ifstream(file, std::ios::binary).read(reinterpret_cast<char*>(b), sizeof b);
			if (b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') return "image/png";
			if (b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF) return "image/jpeg";
			if (b[0] == 'G' && b[1] == 'I' && b[2] == 'F') return "image/gif";
			if (b[0] == 'B' && b[1] == 'M') return "image/bmp";
			if (std::memcmp(b, "RIFF", 4) == 0 && std::memcmp(b + 8, "WEBP", 4) == 0) return "image/webp";
			return "";
		}

		// A file's address, as a text/uri-list names it.
		std::string address_of(const std::filesystem::path& file) {
			static constexpr char hex[] = "0123456789ABCDEF";
			std::string out = "file://";
			for (unsigned char ch : file.string()) {
				if (std::isalnum(ch) || std::strchr("/-._~", ch)) out += static_cast<char>(ch);
				else out += {'%', hex[ch >> 4], hex[ch & 15]};
			}
			return out;
		}

		// The file under the name it's carried as, with its kind's extension: a link to it
		// in a folder of this program's own, emptied each time, so what it's let go over
		// reads it by that name. The file itself, if there's no such folder.
		std::filesystem::path named(const std::filesystem::path& file, std::string name, const std::string& kind) {
			static const std::map<std::string, std::string> extensions{{"image/png", ".png"}, {"image/jpeg", ".jpg"}, {"image/gif", ".gif"}, {"image/bmp", ".bmp"}, {"image/webp", ".webp"}};
			std::ranges::replace(name, '/', '-');
			if (name.empty() || name.starts_with('.')) name = file.filename().string();
			if (auto ext = extensions.find(kind); ext != extensions.end() && std::filesystem::path(name).extension().empty()) name += ext->second;
			const char* runtime = std::getenv("XDG_RUNTIME_DIR");
			std::error_code failed;
			auto folder = std::filesystem::path(runtime && *runtime ? runtime : std::filesystem::temp_directory_path(failed).string()) / ("carried-" + std::to_string(getpid()));
			std::filesystem::remove_all(folder, failed);
			std::filesystem::create_directories(folder, failed);
			std::filesystem::create_symlink(std::filesystem::absolute(file, failed), folder / name, failed);
			return failed ? file : folder / name;
		}

		void stop_carrying_out(wl_data_source* source) {
			auto& c = owning->input->clipboard;
			if (c.carried_out.get() != source) return;
			c.carried_out.reset();
			c.carried_icon.reset();
			c.carried_pixels.reset();
		}

		// A file carried out, sent as it's asked for, away from the window's thread, so a
		// large one doesn't hold the window while it's written.
		const wl_data_source_listener carried_listener{
		    .target = [](void*, wl_data_source*, const char*) {},
		    .send =
		        [](void*, wl_data_source*, const char* mime, std::int32_t fd) {
			        auto& c = owning->input->clipboard;
			        std::thread([to = owned_descriptor{fd}, as_file = std::strcmp(mime, files) == 0, file = c.carried_file] {
				        std::string bytes;
				        if (as_file) bytes = address_of(file) + "\r\n";
				        else bytes.assign(std::istreambuf_iterator<char>(std::ifstream(file, std::ios::binary).rdbuf()), {});
				        for (std::size_t sent = 0; sent < bytes.size();) {
					        ssize_t wrote = write(to.get(), bytes.data() + sent, bytes.size() - sent);
					        if (wrote <= 0) break;
					        sent += static_cast<std::size_t>(wrote);
				        }
			        }).detach();
		        },
		    .cancelled = [](void*, wl_data_source* source) { stop_carrying_out(source); },
		    .dnd_drop_performed = [](void*, wl_data_source*) {},
		    .dnd_finished = [](void*, wl_data_source* source) { stop_carrying_out(source); },
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
			c.turned_away.reset();
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
			        // What we're carrying out ourselves isn't taken back in, as it'd wait on
			        // this thread to send it.
			        if (!ours || c.carried_out || (!c.coming_files && c.coming_picture.empty())) {
				        wl_data_offer_accept(offer, serial, nullptr);
				        c.turned_away.reset(offer);
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

	void copy_picture(const picture& p) {
		std::vector<std::uint8_t> png = p.png();
		if (png.empty()) return;
		if (!owning || !owning->input || !owning->input->clipboard.device) {
			kept_here.clear();
			picture_kept_here = std::move(png);
			return;
		}
		auto& s = *owning->input;
		auto& c = s.clipboard;
		c.copied.assign(png.begin(), png.end());
		c.copied_picture = true;
		c.source = wl_data_device_manager_create_data_source(owning->data_devices);
		wl_data_source_add_listener(c.source, &source_listener, nullptr);
		wl_data_source_offer(c.source, "image/png");
		wl_data_device_set_selection(c.device, c.source, s.input_serial);
	}

	namespace {

		// A small copy of a picture on a surface of its own, for under the pointer as it's
		// carried, its middle there; nothing where there's no shared memory to put it in.
		void carry_icon(clipboard& c, const picture& shown) {
			if (!shown || !owning->shm || !owning->compositor) return;
			picture small = shown.smaller(160);
			int w = small.width(), h = small.height();
			std::size_t bytes = static_cast<std::size_t>(w) * h * 4;
			int fd = memfd_create("carried", MFD_CLOEXEC);
			if (fd < 0) return;
			owned_descriptor held{fd};
			if (ftruncate(fd, static_cast<off_t>(bytes)) != 0) return;
			void* mapped = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
			if (mapped == MAP_FAILED) return;
			// ARGB8888 is blue, green, red and alpha in memory, each color times its alpha.
			auto* to = static_cast<std::uint8_t*>(mapped);
			const auto& from = small.rgba();
			for (std::size_t i = 0; i < bytes; i += 4) {
				unsigned a = from[i + 3];
				to[i] = static_cast<std::uint8_t>(from[i + 2] * a / 255);
				to[i + 1] = static_cast<std::uint8_t>(from[i + 1] * a / 255);
				to[i + 2] = static_cast<std::uint8_t>(from[i] * a / 255);
				to[i + 3] = static_cast<std::uint8_t>(a);
			}
			munmap(mapped, bytes);
			wl_shm_pool* pool = wl_shm_create_pool(owning->shm.get(), fd, static_cast<std::int32_t>(bytes));
			c.carried_pixels.reset(wl_shm_pool_create_buffer(pool, 0, w, h, w * 4, WL_SHM_FORMAT_ARGB8888));
			wl_shm_pool_destroy(pool);
			c.carried_icon.reset(wl_compositor_create_surface(owning->compositor.get()));
			if (owning->compositor_version >= WL_SURFACE_OFFSET_SINCE_VERSION) {
				wl_surface_attach(c.carried_icon.get(), c.carried_pixels.get(), 0, 0);
				wl_surface_offset(c.carried_icon.get(), -w / 2, -h / 2);
			} else {
				wl_surface_attach(c.carried_icon.get(), c.carried_pixels.get(), -w / 2, -h / 2);
			}
			wl_surface_damage(c.carried_icon.get(), 0, 0, w, h);
			wl_surface_commit(c.carried_icon.get());
		}

	}

	void carry_out(const std::filesystem::path& file, const std::string& name, const picture& shown) {
		if (!owning || !owning->input || !owning->input->clipboard.device || !owning->input->pointer_over) return;
		auto& s = *owning->input;
		auto& c = s.clipboard;
		c.carried_picture = picture_kind(file);
		c.carried_file = named(file, name, c.carried_picture);
		c.carried_out = wl_data_device_manager_create_data_source(owning->data_devices);
		wl_data_source_add_listener(c.carried_out, &carried_listener, nullptr);
		wl_data_source_offer(c.carried_out, files);
		if (!c.carried_picture.empty()) wl_data_source_offer(c.carried_out, c.carried_picture.c_str());
		if (wl_data_source_get_version(c.carried_out) >= WL_DATA_SOURCE_SET_ACTIONS_SINCE_VERSION) wl_data_source_set_actions(c.carried_out, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
		carry_icon(c, shown);
		wl_data_device_start_drag(c.device, c.carried_out, s.pointer_over->surface.get(), c.carried_icon.get(), s.input_serial);
	}

	std::optional<std::vector<std::uint8_t>> clipboard_picture() {
		if (!owning || !owning->input || !owning->input->clipboard.device) return picture_kept_here;
		auto& c = owning->input->clipboard;
		// Ours, which we'd otherwise wait on ourselves to send.
		if (c.source) return c.copied_picture ? std::optional(std::vector<std::uint8_t>(c.copied.begin(), c.copied.end())) : std::nullopt;
		if (!c.offered || c.offered_picture.empty()) return std::nullopt;
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
				picture_kept_here.reset();
				return;
			}
			auto& s = *owning->input;
			auto& c = s.clipboard;
			c.copied = std::move(text);
			c.copied_picture = false;
			c.source = wl_data_device_manager_create_data_source(owning->data_devices);
			wl_data_source_add_listener(c.source, &source_listener, nullptr);
			wl_data_source_offer(c.source, utf8);
			wl_data_source_offer(c.source, "text/plain");
			wl_data_source_offer(c.source, "UTF8_STRING");
			wl_data_device_set_selection(c.device, c.source, s.input_serial);
		}

		std::optional<std::string> pasted_text() {
			if (!owning || !owning->input || !owning->input->clipboard.device) return picture_kept_here ? std::nullopt : std::optional(kept_here);
			auto& c = owning->input->clipboard;
			if (c.source) return c.copied_picture ? std::nullopt : std::optional(c.copied);  // ours, which we'd otherwise wait on ourselves to send
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
