// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// The app and its windows on Wayland: the globals it binds, each window's surface
// and how the compositor sizes and scales it, and the loop that reads the socket,
// runs what other threads post, and draws what changed when the compositor asks for
// a frame.

#include "one/desktop/app.hpp"

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <optional>
#include <vector>

#include "app_internal.hpp"
#include "ui_internal.hpp"

namespace one::desktop {

	namespace {

		// Wayland's listeners are C function pointers; a lambda without captures is one,
		// with the object it's for as the first argument.
		template <class T>
		T* as(void* data) {
			return static_cast<T*>(data);
		}

		const xdg_wm_base_listener shell_listener{
		    .ping = [](void*, xdg_wm_base* shell, std::uint32_t serial) { xdg_wm_base_pong(shell, serial); },
		};

		const wl_registry_listener registry_listener{
		    .global =
		        [](void* data, wl_registry* registry, std::uint32_t name, const char* interface, std::uint32_t version) {
			        auto* s = as<app::state>(data);
			        auto is = [&](const wl_interface& i) { return std::strcmp(interface, i.name) == 0; };
			        if (is(wl_compositor_interface)) {
				        s->compositor_version = std::min(version, 6u);
				        s->compositor = static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, s->compositor_version));
			        } else if (is(xdg_wm_base_interface)) {
				        s->shell = static_cast<xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
				        xdg_wm_base_add_listener(s->shell, &shell_listener, nullptr);
			        } else if (is(zxdg_decoration_manager_v1_interface)) {
				        s->decorations = static_cast<zxdg_decoration_manager_v1*>(wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1));
			        } else if (is(wp_fractional_scale_manager_v1_interface)) {
				        s->fractional =
				            static_cast<wp_fractional_scale_manager_v1*>(wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, 1));
			        } else if (is(wp_viewporter_interface)) {
				        s->viewporter = static_cast<wp_viewporter*>(wl_registry_bind(registry, name, &wp_viewporter_interface, 1));
			        } else if (is(wp_cursor_shape_manager_v1_interface)) {
				        s->cursors = static_cast<wp_cursor_shape_manager_v1*>(wl_registry_bind(registry, name, &wp_cursor_shape_manager_v1_interface, 1));
			        } else if (is(wl_seat_interface) && !s->input) {
				        bind_seat(*s, registry, name, version);
			        }
		        },
		    .global_remove = [](void*, wl_registry*, std::uint32_t) {},
		};

		const wl_callback_listener frame_listener{
		    .done =
		        [](void* data, wl_callback* callback, std::uint32_t) {
			        (void)callback;
			        as<window::state>(data)->frame.reset();
		        },
		};

		const xdg_surface_listener shell_surface_listener{
		    .configure =
		        [](void* data, xdg_surface* surface, std::uint32_t serial) {
			        auto* w = as<window::state>(data);
			        xdg_surface_ack_configure(surface, serial);
			        if (w->asked.width > 0) w->size.width = w->asked.width;
			        if (w->asked.height > 0) w->size.height = w->asked.height;
			        w->configured = true;
			        w->dirty = true;
		        },
		};

		const xdg_toplevel_listener toplevel_listener{
		    .configure = [](void* data, xdg_toplevel*, std::int32_t width, std::int32_t height,
		                    wl_array*) { as<window::state>(data)->asked = {width, height}; },
		    .close = [](void* data, xdg_toplevel*) { as<window::state>(data)->closed = true; },
		    .configure_bounds = [](void*, xdg_toplevel*, std::int32_t, std::int32_t) {},
		    .wm_capabilities = [](void*, xdg_toplevel*, wl_array*) {},
		};

		const wp_fractional_scale_v1_listener fraction_listener{
		    .preferred_scale =
		        [](void* data, wp_fractional_scale_v1*, std::uint32_t scale) {
			        auto* w = as<window::state>(data);
			        w->scale = scale / 120.0;
			        w->dirty = true;
		        },
		};

		// Without fractional-scale, the surface says the whole-number scale it would
		// like; a buffer that scale is then drawn without the viewport.
		const wl_surface_listener surface_listener{
		    .enter = [](void*, wl_surface*, wl_output*) {},
		    .leave = [](void*, wl_surface*, wl_output*) {},
		    .preferred_buffer_scale =
		        [](void* data, wl_surface* surface, std::int32_t factor) {
			        auto* w = as<window::state>(data);
			        if (w->fraction) return;
			        w->scale = factor;
			        if (!w->viewport) wl_surface_set_buffer_scale(surface, factor);
			        w->dirty = true;
		        },
		    .preferred_buffer_transform = [](void*, wl_surface*, std::uint32_t) {},
		};

	}  // namespace

	window::state::~state() {
		if (owner && owner->input) {
			if (owner->input->pointer_over == this) owner->input->pointer_over = nullptr;
			if (owner->input->keyboard_in == this) owner->input->keyboard_in = nullptr;
		}
	}

	// window

	window::window(std::unique_ptr<state> s) : state_(std::move(s)) {}
	window::~window() = default;
	one::desktop::size window::size() const { return state_->size; }
	double window::scale() const { return state_->scale; }
	void window::redraw() { state_->dirty = true; }
	void window::show(std::function<view()> build) {
		state_->build = std::move(build);
		state_->needs_building = true;
		state_->dirty = true;
	}
	void window::rebuild() {
		state_->needs_building = true;
		state_->dirty = true;
	}
	void window::look(const one::desktop::theme& t) {
		state_->look = t;
		background = t.page;
		rebuild();
	}
	void window::title(const std::string& text) { xdg_toplevel_set_title(state_->toplevel, text.c_str()); }

	// app

	app::app(std::unique_ptr<state> s) : state_(std::move(s)) {}
	app::app(app&&) noexcept = default;
	app& app::operator=(app&&) noexcept = default;
	app::~app() = default;

	std::expected<app, std::string> app::connect() {
		auto s = std::make_unique<state>();
		s->display = wl_display_connect(nullptr);
		if (!s->display) return std::unexpected("there's no Wayland compositor to connect to; is WAYLAND_DISPLAY set?");
		s->registry = wl_display_get_registry(s->display);
		wl_registry_add_listener(s->registry, &registry_listener, s.get());
		wl_display_roundtrip(s->display);
		if (!s->compositor || !s->shell) return std::unexpected("the compositor doesn't offer wl_compositor and xdg_wm_base, which windows need");
		try {
			s->gpu.emplace(s->display);
		} catch (const std::exception& e) {
			return std::unexpected(std::string("Vulkan: ") + e.what());
		}
		s->wake = owned_descriptor(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
		return app{std::move(s)};
	}

	window& app::open(const window_options& options) {
		auto w = std::make_unique<window::state>();
		w->owner = state_.get();
		w->size = options.size;
		w->surface = wl_compositor_create_surface(state_->compositor);
		if (state_->compositor_version >= 6) wl_surface_add_listener(w->surface, &surface_listener, w.get());
		if (state_->viewporter) w->viewport = wp_viewporter_get_viewport(state_->viewporter, w->surface);
		if (state_->fractional && w->viewport) {
			w->fraction = wp_fractional_scale_manager_v1_get_fractional_scale(state_->fractional, w->surface);
			wp_fractional_scale_v1_add_listener(w->fraction, &fraction_listener, w.get());
		}
		w->shell_surface = xdg_wm_base_get_xdg_surface(state_->shell, w->surface);
		xdg_surface_add_listener(w->shell_surface, &shell_surface_listener, w.get());
		w->toplevel = xdg_surface_get_toplevel(w->shell_surface);
		xdg_toplevel_add_listener(w->toplevel, &toplevel_listener, w.get());
		xdg_toplevel_set_title(w->toplevel, options.title.c_str());
		if (!options.id.empty()) xdg_toplevel_set_app_id(w->toplevel, options.id.c_str());
		if (state_->decorations) {
			w->decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(state_->decorations, w->toplevel);
			zxdg_toplevel_decoration_v1_set_mode(w->decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
		}
		// Committed with nothing in it, the compositor answers with a configure.
		wl_surface_commit(w->surface);
		w->drawn.emplace(*state_->gpu, state_->display, w->surface);
		auto* inside = w.get();
		state_->windows.push_back(std::make_unique<window>(std::move(w)));
		inside->outside = state_->windows.back().get();
		state_->windows.back()->background = inside->look.page;
		return *state_->windows.back();
	}

	void app::post(std::function<void()> task) {
		{
			std::lock_guard hold(state_->lock);
			state_->posted.push_back(std::move(task));
		}
		std::uint64_t one = 1;
		(void)!write(state_->wake.get(), &one, sizeof one);
	}

	void app::quit() { state_->quitting = true; }

	// Draws a window that changed, if the compositor is ready for its next frame:
	// asks for the frame after, then presents, which commits both.
	static bool ready(window& shown) {
		auto& w = shown.internals();
		if (w.needs_building || w.needs_placing) w.dirty = true;
		return w.configured && w.dirty && !w.frame && w.size.width > 0 && w.size.height > 0;
	}
	static void draw(window& shown) {
		auto& w = shown.internals();
		if (!ready(shown)) return;
		w.dirty = false;
		if (w.viewport) wp_viewport_set_destination(w.viewport, w.size.width, w.size.height);
		w.frame = wl_surface_frame(w.surface);
		wl_callback_add_listener(w.frame, &frame_listener, &w);
		if (!w.painter) w.painter.emplace(*w.owner->gpu, w.drawn->format());
		bring_up_to_date(w);
		one::desktop::size pixels = w.pixels();
		gpu::surface::work work{
		    .prepare =
		        [&](const vk::raii::CommandBuffer& commands, std::size_t slot) {
			        w.painter->begin(pixels, w.scale, slot);
			        canvas c{*w.painter};
			        if (w.root) {
				        detail::theme_in_use() = &w.look;
				        detail::layout_scale() = w.scale;
				        w.root->paint(c);
				        detail::theme_in_use() = nullptr;
			        }
			        if (shown.on_draw) shown.on_draw(c);
			        w.painter->upload(commands);
		        },
		    .record = [&](const vk::raii::CommandBuffer& commands) { w.painter->draw(commands); },
		};
		if (!w.drawn->draw(pixels, shown.background, work)) w.dirty = true;
	}

	int app::run() {
		state& s = *state_;
		int fd = wl_display_get_fd(s.display);
		while (!s.quitting && !s.windows.empty()) {
			// Closed windows go, asking first whoever wanted to know.
			for (auto& w : s.windows) {
				if (w->internals().closed && w->on_close) {
					w->internals().closed = false;
					w->on_close();
				}
			}
			std::erase_if(s.windows, [](const auto& w) { return w->internals().closed; });
			if (s.windows.empty()) break;
			for (auto& w : s.windows) draw(*w);

			// Events already read, like a configure the GPU driver's own reading took in,
			// are dispatched first; a window they made ready is drawn before waiting.
			while (wl_display_prepare_read(s.display) != 0) wl_display_dispatch_pending(s.display);
			if (std::ranges::any_of(s.windows, [](const auto& w) { return ready(*w); })) {
				wl_display_cancel_read(s.display);
				continue;
			}
			wl_display_flush(s.display);
			int repeat = s.input ? s.input->repeat_timer.get() : -1;
			std::array<pollfd, 3> fds{pollfd{.fd = fd, .events = POLLIN, .revents = 0}, pollfd{.fd = s.wake.get(), .events = POLLIN, .revents = 0},
			                          pollfd{.fd = repeat, .events = POLLIN, .revents = 0}};
			if (poll(fds.data(), fds.size(), -1) < 0) {
				wl_display_cancel_read(s.display);
				continue;
			}
			if (fds[0].revents & POLLIN) {
				if (wl_display_read_events(s.display) < 0) return 1;
			} else {
				wl_display_cancel_read(s.display);
			}
			if (fds[0].revents & (POLLERR | POLLHUP)) return 1;
			if (wl_display_dispatch_pending(s.display) < 0) return 1;
			if (repeat >= 0 && (fds[2].revents & POLLIN)) repeat_key(s);
			if (fds[1].revents & POLLIN) {
				std::uint64_t count = 0;
				(void)!read(s.wake.get(), &count, sizeof count);
				std::vector<std::function<void()>> tasks;
				{
					std::lock_guard hold(s.lock);
					tasks.swap(s.posted);
				}
				for (auto& task : tasks) task();
			}
		}
		return 0;
	}

}  // namespace one::desktop
