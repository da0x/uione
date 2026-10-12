// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// What the app and its windows keep: the compositor's globals, the seat's pointer and
// keyboard, and for each window its surfaces, its swapchain and the view it shows.

#pragma once

#include <cmath>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "cursor-shape-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "gpu.hpp"
#include "loom/app.hpp"
#include "loom/state.hpp"
#include "loom/theme.hpp"
#include "loom/ui.hpp"
#include "renderer.hpp"
#include "viewporter-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

namespace loom {

	// The seat: its pointer and keyboard, which window each is over, and the keyboard's
	// layout and repeat.
	struct seat {
		wl_seat* handle = nullptr;
		wl_pointer* pointer = nullptr;
		wl_keyboard* keyboard = nullptr;
		wp_cursor_shape_device_v1* cursor = nullptr;
		std::uint32_t pointer_serial = 0;  // the last enter's, which a cursor shape is set with
		window::state* pointer_over = nullptr;
		window::state* keyboard_in = nullptr;
		point pointer_at;
		float wheel_x = 0, wheel_y = 0;  // since the last frame of pointer events
		int shape = -1;                  // the cursor shape last set

		xkb_context* keys = nullptr;
		xkb_keymap* keymap = nullptr;
		xkb_state* key_state = nullptr;
		int repeat_rate = 25, repeat_delay = 600;  // per second, and milliseconds before
		int repeat_timer = -1;                     // a timerfd
		std::uint32_t repeating = 0;               // the key held, or 0

		~seat();
	};

	struct app::state {
		wl_display* display = nullptr;
		wl_registry* registry = nullptr;
		wl_compositor* compositor = nullptr;
		std::uint32_t compositor_version = 0;
		xdg_wm_base* shell = nullptr;
		zxdg_decoration_manager_v1* decorations = nullptr;
		wp_fractional_scale_manager_v1* fractional = nullptr;
		wp_viewporter* viewporter = nullptr;
		wp_cursor_shape_manager_v1* cursors = nullptr;
		std::optional<gpu::device> gpu;
		std::optional<loom::seat> input;
		std::vector<std::unique_ptr<window>> windows;

		int wake = -1;  // an eventfd post() writes to
		std::mutex lock;
		std::vector<std::function<void()>> posted;
		bool quitting = false;

		~state();
	};

	struct window::state {
		app::state* owner = nullptr;
		window* outside = nullptr;  // the window this is the state of
		wl_surface* surface = nullptr;
		xdg_surface* shell_surface = nullptr;
		xdg_toplevel* toplevel = nullptr;
		zxdg_toplevel_decoration_v1* decoration = nullptr;
		wp_fractional_scale_v1* fraction = nullptr;
		wp_viewport* viewport = nullptr;
		wl_callback* frame = nullptr;
		std::optional<gpu::surface> drawn;
		std::optional<loom::renderer> painter;

		loom::size size{};
		loom::size asked{};  // what the compositor's last configure said, 0 for ours
		double scale = 1;    // from fractional-scale, or the surface's preferred integer scale
		bool configured = false;
		bool dirty = true;
		bool closed = false;

		// The view it shows: how it's built, what was built, and whether it needs
		// building or placing again.
		loom::theme look = theme::harbor();
		std::function<view()> build;
		std::unique_ptr<element> root;
		std::shared_ptr<detail::reader> reading;
		bool needs_building = false;
		bool needs_placing = false;
		loom::size placed_at{};
		double placed_scale = 0;
		element* pressed = nullptr;

		loom::size pixels() const {
			return {static_cast<int>(std::lround(size.width * scale)), static_cast<int>(std::lround(size.height * scale))};
		}
		void redraw() { dirty = true; }
		~state();
	};

	// The view's part of a window: building it again, placing it, and what the pointer
	// and keyboard do to it.
	void bring_up_to_date(window::state& w);
	void deliver_key(window::state& w, const key_event& e);
	void deliver_pointer(window::state& w, point at);
	void deliver_press(window::state& w, point at);
	void deliver_release(window::state& w, point at);
	void deliver_wheel(window::state& w, point at, float dx, float dy);
	void deliver_leave(window::state& w);
	loom::cursor cursor_at(window::state& w, point at);

	// The seat's part of the app, in seat.cpp.
	void bind_seat(app::state& a, wl_registry* registry, std::uint32_t name, std::uint32_t version);
	void repeat_key(app::state& a);

}  // namespace loom
