// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// What the app and its windows keep: the compositor's globals, the seat's pointer and
// keyboard, and for each window its surfaces, its swapchain and the view it shows.

#pragma once

#include <array>
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
#include "owned.hpp"
#include "one/desktop/app.hpp"
#include "one/desktop/state.hpp"
#include "one/desktop/theme.hpp"
#include "one/desktop/ui.hpp"
#include "renderer.hpp"
#include "text-input-unstable-v3-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

namespace one::desktop {

	// Wayland's protocol functions are static inline, so each is given here a name that
	// an owned<> can hold: released::wl_surface_destroy, and the rest.
	namespace released {
		inline void wl_registry_destroy(wl_registry* held) { ::wl_registry_destroy(held); }
		inline void wl_compositor_destroy(wl_compositor* held) { ::wl_compositor_destroy(held); }
		inline void xdg_wm_base_destroy(xdg_wm_base* held) { ::xdg_wm_base_destroy(held); }
		inline void zxdg_decoration_manager_v1_destroy(zxdg_decoration_manager_v1* held) { ::zxdg_decoration_manager_v1_destroy(held); }
		inline void wp_fractional_scale_manager_v1_destroy(wp_fractional_scale_manager_v1* held) { ::wp_fractional_scale_manager_v1_destroy(held); }
		inline void wp_viewporter_destroy(wp_viewporter* held) { ::wp_viewporter_destroy(held); }
		inline void wp_cursor_shape_manager_v1_destroy(wp_cursor_shape_manager_v1* held) { ::wp_cursor_shape_manager_v1_destroy(held); }
		inline void wl_surface_destroy(wl_surface* held) { ::wl_surface_destroy(held); }
		inline void xdg_surface_destroy(xdg_surface* held) { ::xdg_surface_destroy(held); }
		inline void xdg_toplevel_destroy(xdg_toplevel* held) { ::xdg_toplevel_destroy(held); }
		inline void zxdg_toplevel_decoration_v1_destroy(zxdg_toplevel_decoration_v1* held) { ::zxdg_toplevel_decoration_v1_destroy(held); }
		inline void wp_fractional_scale_v1_destroy(wp_fractional_scale_v1* held) { ::wp_fractional_scale_v1_destroy(held); }
		inline void wp_viewport_destroy(wp_viewport* held) { ::wp_viewport_destroy(held); }
		inline void wl_callback_destroy(wl_callback* held) { ::wl_callback_destroy(held); }
		inline void wl_seat_release(wl_seat* held) { ::wl_seat_release(held); }
		inline void wl_keyboard_release(wl_keyboard* held) { ::wl_keyboard_release(held); }
		inline void wl_pointer_release(wl_pointer* held) { ::wl_pointer_release(held); }
		inline void wp_cursor_shape_device_v1_destroy(wp_cursor_shape_device_v1* held) { ::wp_cursor_shape_device_v1_destroy(held); }
		inline void wl_data_device_manager_destroy(wl_data_device_manager* held) { ::wl_data_device_manager_destroy(held); }
		inline void wl_data_device_release(wl_data_device* held) { ::wl_data_device_release(held); }
		inline void wl_data_offer_destroy(wl_data_offer* held) { ::wl_data_offer_destroy(held); }
		inline void wl_data_source_destroy(wl_data_source* held) { ::wl_data_source_destroy(held); }
		inline void zwp_text_input_manager_v3_destroy(zwp_text_input_manager_v3* held) { ::zwp_text_input_manager_v3_destroy(held); }
		inline void zwp_text_input_v3_destroy(zwp_text_input_v3* held) { ::zwp_text_input_v3_destroy(held); }
	}  // namespace released

	// The clipboard, through the seat's data device: what we offer, and what's offered.
	struct clipboard {
		owned<wl_data_source, released::wl_data_source_destroy> source;  // ours, while it's the clipboard
		std::string copied;                                              // what it offers
		owned<wl_data_offer, released::wl_data_offer_destroy> offered;   // another program's, while it's the clipboard
		std::string offered_type;                                        // the kind of text it's read as
		wl_data_offer* coming = nullptr;                                 // an offer whose types are arriving
		std::string coming_type;
		owned<wl_data_device, released::wl_data_device_release> device;
	};

	// What an input method was last told of the field with the keyboard.
	struct typing_told {
		text_entry* entry = nullptr;
		std::string text;
		std::size_t caret = 0, anchor = 0;
		std::array<int, 4> area{};  // the caret's, in the window
		bool operator==(const typing_told&) const = default;
	};

	// The seat's text input, and what an input method has said since it was last done.
	struct typing {
		bool entered = false;  // the compositor says it's in one of our windows
		bool enabled = false;
		std::uint32_t commits = 0;
		typing_told told;
		std::string pending_preedit, pending_commit;
		int pending_caret = 0;
		std::uint32_t pending_before = 0, pending_after = 0;
		owned<zwp_text_input_v3, released::zwp_text_input_v3_destroy> handle;
	};

	// The seat: its pointer and keyboard, which window each is over, and the keyboard's
	// layout and repeat. Its members go in the reverse of their order here: the cursor
	// before the pointer it's for, and the keymap's state before the keymap.
	struct seat {
		owned<xkb_context, xkb_context_unref> keys;
		owned<xkb_keymap, xkb_keymap_unref> keymap;
		owned<xkb_state, xkb_state_unref> key_state;
		owned_descriptor repeat_timer;  // a timerfd
		owned<wl_seat, released::wl_seat_release> handle;
		owned<wl_keyboard, released::wl_keyboard_release> keyboard;
		owned<wl_pointer, released::wl_pointer_release> pointer;
		owned<wp_cursor_shape_device_v1, released::wp_cursor_shape_device_v1_destroy> cursor;
		one::desktop::clipboard clipboard;
		one::desktop::typing typing;
		std::uint32_t input_serial = 0;  // the last key's or button's, which the clipboard is set with
		std::uint32_t pressed_at_time = 0;  // the last press: when, where, and how many came quickly before
		point pressed_at;
		int clicks = 0;
		std::uint32_t pointer_serial = 0;  // the last enter's, which a cursor shape is set with
		window::state* pointer_over = nullptr;
		window::state* keyboard_in = nullptr;
		point pointer_at;
		float wheel_x = 0, wheel_y = 0;  // since the last frame of pointer events
		int shape = -1;                  // the cursor shape last set
		int repeat_rate = 25, repeat_delay = 600;  // per second, and milliseconds before
		std::uint32_t repeating = 0;               // the key held, or 0
	};

	// The app's globals, freed after every window and the seat, the display last.
	struct app::state {
		owned_descriptor wake;  // an eventfd post() writes to
		owned<wl_display, wl_display_disconnect> display;
		owned<wl_registry, released::wl_registry_destroy> registry;
		owned<wl_compositor, released::wl_compositor_destroy> compositor;
		std::uint32_t compositor_version = 0;
		owned<xdg_wm_base, released::xdg_wm_base_destroy> shell;
		owned<zxdg_decoration_manager_v1, released::zxdg_decoration_manager_v1_destroy> decorations;
		owned<wp_fractional_scale_manager_v1, released::wp_fractional_scale_manager_v1_destroy> fractional;
		owned<wp_viewporter, released::wp_viewporter_destroy> viewporter;
		owned<wp_cursor_shape_manager_v1, released::wp_cursor_shape_manager_v1_destroy> cursors;
		owned<wl_data_device_manager, released::wl_data_device_manager_destroy> data_devices;
		owned<zwp_text_input_manager_v3, released::zwp_text_input_manager_v3_destroy> text_inputs;
		std::optional<gpu::device> gpu;
		std::optional<one::desktop::seat> input;
		std::vector<std::unique_ptr<window>> windows;

		std::mutex lock;
		std::vector<std::function<void()>> posted;
		bool quitting = false;
	};

	struct window::state {
		app::state* owner = nullptr;
		window* outside = nullptr;  // the window this is the state of
		// The surface and its roles, freed after the swapchain drawn on it, the surface
		// itself last.
		owned<wl_surface, released::wl_surface_destroy> surface;
		owned<xdg_surface, released::xdg_surface_destroy> shell_surface;
		owned<xdg_toplevel, released::xdg_toplevel_destroy> toplevel;
		owned<zxdg_toplevel_decoration_v1, released::zxdg_toplevel_decoration_v1_destroy> decoration;
		owned<wp_fractional_scale_v1, released::wp_fractional_scale_v1_destroy> fraction;
		owned<wp_viewport, released::wp_viewport_destroy> viewport;
		owned<wl_callback, released::wl_callback_destroy> frame;
		std::optional<gpu::surface> drawn;
		std::optional<one::desktop::renderer> painter;

		one::desktop::size size{};
		one::desktop::size asked{};  // what the compositor's last configure said, 0 for ours
		double scale = 1;    // from fractional-scale, or the surface's preferred integer scale
		bool configured = false;
		bool dirty = true;
		bool closed = false;

		// The view it shows: how it's built, what was built, and whether it needs
		// building or placing again.
		one::desktop::theme look = theme::harbor();
		std::function<view()> build;
		std::unique_ptr<element> root;
		std::shared_ptr<detail::reader> reading;
		bool needs_building = false;
		bool needs_placing = false;
		one::desktop::size placed_at{};
		double placed_scale = 0;
		element* pressed = nullptr;
		element* dragging = nullptr;  // what took the press, and hears the pointer move while it's held

		one::desktop::size pixels() const {
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
	void deliver_press(window::state& w, point at, int clicks);
	void deliver_release(window::state& w, point at);
	void deliver_wheel(window::state& w, point at, float dx, float dy);
	void deliver_leave(window::state& w);
	one::desktop::cursor cursor_at(window::state& w, point at);

	// The seat's part of the app, in seat.cpp.
	void bind_seat(app::state& a, wl_registry* registry, std::uint32_t name, std::uint32_t version);
	void repeat_key(app::state& a);

	// The clipboard's part, in clipboard.cpp: its device, once both the seat and the
	// data device manager are bound.
	void connect_clipboard(app::state& a);

	// Input methods' part, in text_input.cpp: the seat's text input, once there's both
	// a seat and the manager; and telling it about the field with the keyboard, after
	// each frame is drawn.
	void connect_text_input(app::state& a);
	void tell_input_method(app::state& a);

}  // namespace one::desktop
