// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// loom: a modern C++ toolkit for Wayland, drawing with Vulkan.
//
// An app connects to the compositor and runs the loop; it opens windows, and each
// window is drawn on the GPU when something in it changes, paced by the compositor:
//
//     auto app = loom::app::connect();
//     if (!app) return std::println(stderr, "{}", app.error()), 1;
//     auto& window = app->open({.title = "Hello", .size = {900, 640}});
//     window.background = loom::color::hex(0xf7f8fa);
//     return app->run();
//
// Everything runs on the thread that calls run(), except post(), which any thread
// may call to hand work to the loop: how a listener on another thread says
// something changed.

#pragma once

#include <cmath>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>

namespace loom {

	// A size in logical pixels, the ones layout speaks in. On a screen scaled by
	// 1.5, a window 800 wide is 1200 pixels on the GPU.
	struct size {
		int width = 0;
		int height = 0;
		bool operator==(const size&) const = default;
	};

	// A color as linear-light floats, which the GPU blends in; made from the sRGB hex
	// a designer writes.
	struct color {
		float red = 0, green = 0, blue = 0, alpha = 1;

		static constexpr float linear(float srgb) {
			return srgb <= 0.04045f ? srgb / 12.92f : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
		}
		static constexpr color hex(std::uint32_t rgb, float alpha = 1) {
			return {linear(((rgb >> 16) & 0xff) / 255.f), linear(((rgb >> 8) & 0xff) / 255.f), linear((rgb & 0xff) / 255.f), alpha};
		}
	};

	class app;

	// What a window is opened with.
	struct window_options {
		std::string title = "loom";
		std::string id = "";  // the app's id the compositor groups its windows by, like org.neotrac.desktop
		loom::size size = {900, 640};
	};

	// A window on the screen. It's drawn again when redraw() is called or the
	// compositor resizes or rescales it.
	class window {
	public:
		~window();
		window(const window&) = delete;
		window& operator=(const window&) = delete;

		loom::color background = color::hex(0xffffff);

		// Its size now, in logical pixels, and how many GPU pixels each is.
		loom::size size() const;
		double scale() const;

		void redraw();
		void title(const std::string& text);

		// Called when the window is asked to close, by its decoration or a shortcut.
		// Closing it by default ends the app when it's the last one.
		std::function<void()> on_close;

		struct state;
		explicit window(std::unique_ptr<state> s);
		state& internals() { return *state_; }

	private:
		std::unique_ptr<state> state_;
	};

	class app {
	public:
		// Connects to the compositor named by WAYLAND_DISPLAY and finds a GPU that
		// can draw on it. Fails with why when there's no Wayland or no Vulkan.
		static std::expected<app, std::string> connect();

		app(app&&) noexcept;
		app& operator=(app&&) noexcept;
		~app();

		window& open(const window_options& options);

		// Runs the loop until quit(), or until the last window closes. Returns 0, or
		// 1 when the compositor went away.
		int run();
		void quit();

		// Hands work to the loop from any thread; it runs on the loop's thread soon
		// after.
		void post(std::function<void()> task);

		struct state;

	private:
		explicit app(std::unique_ptr<state> s);
		std::unique_ptr<state> state_;
	};

}  // namespace loom
