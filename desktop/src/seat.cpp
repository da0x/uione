// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The seat: the pointer, given to the window it's over; the keyboard, through
// xkbcommon's reading of the compositor's layout, repeating a held key on a timer;
// and the cursor's shape, from what's under it.

#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>

#include "app_internal.hpp"

namespace one::desktop {

	namespace {

		app::state* owning = nullptr;  // the app the seat's listeners are for

		window::state* window_of(wl_surface* surface) {
			if (!owning || !surface) return nullptr;
			for (auto& w : owning->windows) {
				if (w->internals().surface == surface) return &w->internals();
			}
			return nullptr;
		}

		void set_cursor(seat& s, one::desktop::cursor c) {
			if (!s.cursor) return;
			int shape = c == cursor::pointer    ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER
			            : c == cursor::text     ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT
			            : c == cursor::grab     ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB
			            : c == cursor::grabbing ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING
			            : c == cursor::resize_across ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_COL_RESIZE
			            : c == cursor::resize_up_down ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ROW_RESIZE
			            : c == cursor::move     ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALL_SCROLL  // four arrows; many themes draw move as a hand
			            : c == cursor::resize_falling ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE
			            : c == cursor::resize_rising ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NESW_RESIZE
			            : c == cursor::crosshair ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR
			                                    : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
			if (shape == s.shape) return;
			s.shape = shape;
			wp_cursor_shape_device_v1_set_shape(s.cursor, s.pointer_serial, static_cast<std::uint32_t>(shape));
		}

		key_event event_for(seat& s, std::uint32_t code) {
			key_event e;
			e.symbol = xkb_state_key_get_one_sym(s.key_state, code);
			char text[64] = {};
			int written = xkb_state_key_get_utf8(s.key_state, code, text, sizeof text);
			auto active = [&](const char* name) { return xkb_state_mod_name_is_active(s.key_state, name, XKB_STATE_MODS_EFFECTIVE) > 0; };
			e.shift = active(XKB_MOD_NAME_SHIFT);
			e.control = active(XKB_MOD_NAME_CTRL);
			e.alt = active(XKB_MOD_NAME_ALT);
			e.super = active(XKB_MOD_NAME_LOGO);
			// Text a key types, not the control characters Enter or Tab make.
			if (written > 0 && static_cast<unsigned char>(text[0]) >= 0x20 && text[0] != 0x7f) e.text.assign(text, static_cast<std::size_t>(written));
			return e;
		}

		void arm(seat& s, int first_milliseconds, int every_milliseconds) {
			itimerspec when{};
			when.it_value.tv_sec = first_milliseconds / 1000;
			when.it_value.tv_nsec = (first_milliseconds % 1000) * 1'000'000L;
			when.it_interval.tv_sec = every_milliseconds / 1000;
			when.it_interval.tv_nsec = (every_milliseconds % 1000) * 1'000'000L;
			timerfd_settime(s.repeat_timer.get(), 0, &when, nullptr);
		}

		const wl_pointer_listener pointer_listener{
		    .enter =
		        [](void*, wl_pointer*, std::uint32_t serial, wl_surface* surface, wl_fixed_t x, wl_fixed_t y) {
			        auto& s = *owning->input;
			        s.pointer_serial = serial;
			        s.shape = -1;
			        s.pointer_over = window_of(surface);
			        s.pointer_at = {static_cast<float>(wl_fixed_to_double(x)), static_cast<float>(wl_fixed_to_double(y))};
			        if (!s.pointer_over) return;
			        deliver_pointer(*s.pointer_over, s.pointer_at);
			        set_cursor(s, cursor_at(*s.pointer_over, s.pointer_at));
		        },
		    .leave =
		        [](void*, wl_pointer*, std::uint32_t, wl_surface*) {
			        auto& s = *owning->input;
			        if (s.pointer_over) deliver_leave(*s.pointer_over);
			        s.pointer_over = nullptr;
		        },
		    .motion =
		        [](void*, wl_pointer*, std::uint32_t, wl_fixed_t x, wl_fixed_t y) {
			        auto& s = *owning->input;
			        s.pointer_at = {static_cast<float>(wl_fixed_to_double(x)), static_cast<float>(wl_fixed_to_double(y))};
			        if (!s.pointer_over) return;
			        deliver_pointer(*s.pointer_over, s.pointer_at);
			        set_cursor(s, cursor_at(*s.pointer_over, s.pointer_at));
		        },
		    .button =
		        [](void*, wl_pointer*, std::uint32_t serial, std::uint32_t time, std::uint32_t button, std::uint32_t state) {
			        auto& s = *owning->input;
			        s.input_serial = serial;
			        if (!s.pointer_over || button != BTN_LEFT) return;
			        if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
				        // A double or triple click: pressed again soon, near where it was.
				        bool again = s.clicks > 0 && time - s.pressed_at_time < 400 && std::abs(s.pointer_at.x - s.pressed_at.x) < 4 &&
				                     std::abs(s.pointer_at.y - s.pressed_at.y) < 4;
				        s.clicks = again ? s.clicks % 3 + 1 : 1;
				        s.pressed_at_time = time;
				        s.pressed_at = s.pointer_at;
				        deliver_press(*s.pointer_over, s.pointer_at, s.clicks);
			        } else {
				        deliver_release(*s.pointer_over, s.pointer_at);
			        }
		        },
		    .axis =
		        [](void*, wl_pointer*, std::uint32_t, std::uint32_t axis, wl_fixed_t value) {
			        auto& s = *owning->input;
			        float by = static_cast<float>(wl_fixed_to_double(value));
			        if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) s.wheel_y += by;
			        else s.wheel_x += by;
		        },
		    // A frame ends a group of pointer events; the wheel's are delivered together.
		    .frame =
		        [](void*, wl_pointer*) {
			        auto& s = *owning->input;
			        if (s.pointer_over && (s.wheel_x != 0 || s.wheel_y != 0)) deliver_wheel(*s.pointer_over, s.pointer_at, s.wheel_x * 2, s.wheel_y * 2);
			        s.wheel_x = s.wheel_y = 0;
		        },
		    .axis_source = [](void*, wl_pointer*, std::uint32_t) {},
		    .axis_stop = [](void*, wl_pointer*, std::uint32_t, std::uint32_t) {},
		    .axis_discrete = [](void*, wl_pointer*, std::uint32_t, std::int32_t) {},
		    .axis_value120 = [](void*, wl_pointer*, std::uint32_t, std::int32_t) {},
		    .axis_relative_direction = [](void*, wl_pointer*, std::uint32_t, std::uint32_t) {},
		    .warp = [](void*, wl_pointer*, wl_fixed_t, wl_fixed_t) {},
		};

		const wl_keyboard_listener keyboard_listener{
		    .keymap =
		        [](void*, wl_keyboard*, std::uint32_t format, std::int32_t fd, std::uint32_t size) {
			        auto& s = *owning->input;
			        owned_descriptor given{fd};
			        if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) return;
			        void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, given.get(), 0);
			        if (mapped == MAP_FAILED) return;
			        xkb_keymap* keymap = xkb_keymap_new_from_string(s.keys, static_cast<const char*>(mapped), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
			        munmap(mapped, size);
			        if (!keymap) return;
			        s.key_state.reset();  // before the keymap it was made from
			        s.keymap = keymap;
			        s.key_state = xkb_state_new(keymap);
		        },
		    .enter = [](void*, wl_keyboard*, std::uint32_t, wl_surface* surface, wl_array*) { owning->input->keyboard_in = window_of(surface); },
		    .leave =
		        [](void*, wl_keyboard*, std::uint32_t, wl_surface*) {
			        auto& s = *owning->input;
			        s.keyboard_in = nullptr;
			        s.repeating = 0;
			        arm(s, 0, 0);
		        },
		    .key =
		        [](void*, wl_keyboard*, std::uint32_t serial, std::uint32_t, std::uint32_t key, std::uint32_t state) {
			        auto& s = *owning->input;
			        s.input_serial = serial;
			        if (!s.key_state) return;
			        std::uint32_t code = key + 8;  // evdev's codes to XKB's
			        if (key == KEY_SPACE) s.space_held = state != WL_KEYBOARD_KEY_STATE_RELEASED;
			        if (state == WL_KEYBOARD_KEY_STATE_RELEASED) {
				        if (s.repeating == code) {
					        s.repeating = 0;
					        arm(s, 0, 0);
				        }
				        return;
			        }
			        if (s.keyboard_in) deliver_key(*s.keyboard_in, event_for(s, code));
			        if (s.repeat_rate > 0 && xkb_keymap_key_repeats(s.keymap, code)) {
				        s.repeating = code;
				        arm(s, s.repeat_delay, 1000 / s.repeat_rate);
			        }
		        },
		    .modifiers =
		        [](void*, wl_keyboard*, std::uint32_t, std::uint32_t depressed, std::uint32_t latched, std::uint32_t locked, std::uint32_t group) {
			        auto& s = *owning->input;
			        if (s.key_state) xkb_state_update_mask(s.key_state, depressed, latched, locked, 0, 0, group);
		        },
		    .repeat_info =
		        [](void*, wl_keyboard*, std::int32_t rate, std::int32_t delay) {
			        auto& s = *owning->input;
			        s.repeat_rate = rate;
			        s.repeat_delay = delay;
		        },
		};

		const wl_seat_listener seat_listener{
		    .capabilities =
		        [](void*, wl_seat* handle, std::uint32_t capabilities) {
			        auto& s = *owning->input;
			        bool pointing = capabilities & WL_SEAT_CAPABILITY_POINTER, typing = capabilities & WL_SEAT_CAPABILITY_KEYBOARD;
			        if (pointing && !s.pointer) {
				        s.pointer = wl_seat_get_pointer(handle);
				        wl_pointer_add_listener(s.pointer, &pointer_listener, nullptr);
				        if (owning->cursors) s.cursor = wp_cursor_shape_manager_v1_get_pointer(owning->cursors, s.pointer);
			        }
			        if (typing && !s.keyboard) {
				        s.keyboard = wl_seat_get_keyboard(handle);
				        wl_keyboard_add_listener(s.keyboard, &keyboard_listener, nullptr);
			        }
		        },
		    .name = [](void*, wl_seat*, const char*) {},
		};

	}  // namespace

	void bind_seat(app::state& a, wl_registry* registry, std::uint32_t name, std::uint32_t version) {
		owning = &a;
		a.input.emplace();
		auto& s = *a.input;
		s.handle = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 9u)));
		s.keys = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		s.repeat_timer = owned_descriptor(timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK));
		wl_seat_add_listener(s.handle, &seat_listener, nullptr);
	}

	void repeat_key(app::state& a) {
		auto& s = *a.input;
		std::uint64_t times = 0;
		(void)!read(s.repeat_timer.get(), &times, sizeof times);
		if (!s.repeating || !s.keyboard_in || !s.key_state) return;
		for (std::uint64_t i = 0; i < std::min<std::uint64_t>(times, 4); ++i) deliver_key(*s.keyboard_in, event_for(s, s.repeating));
	}

	held_keys held() {
		if (!owning || !owning->input || !owning->input->key_state) return {};
		xkb_state* state = owning->input->key_state.get();
		auto active = [&](const char* name) { return xkb_state_mod_name_is_active(state, name, XKB_STATE_MODS_EFFECTIVE) > 0; };
		return {.shift = active(XKB_MOD_NAME_SHIFT), .control = active(XKB_MOD_NAME_CTRL), .alt = active(XKB_MOD_NAME_ALT), .space = owning->input->space_held};
	}

}  // namespace one::desktop
