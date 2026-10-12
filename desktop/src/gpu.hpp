// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The GPU, through Vulkan 1.3: one device shared by every window, and for each window
// a swapchain on its Wayland surface, drawn with dynamic rendering.

#pragma once

#define VK_USE_PLATFORM_WAYLAND_KHR
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "one/desktop/app.hpp"

struct wl_display;
struct wl_surface;

namespace one::desktop::gpu {

	// The device every window draws with, and the queue it draws and presents on.
	struct device {
		vk::raii::Context context;
		vk::raii::Instance instance = nullptr;
		vk::raii::PhysicalDevice physical = nullptr;
		vk::raii::Device logical = nullptr;
		vk::raii::Queue queue = nullptr;
		std::uint32_t family = 0;
		std::string name;  // the GPU's, for saying which was chosen

		// The first GPU that can present to this Wayland display, a discrete one if
		// there is one; with no display, the first that draws, for drawing offscreen.
		explicit device(wl_display* display);

		// Memory of a kind this device has, for what `allowed` (a memory requirement's
		// bits) permits.
		std::uint32_t memory_type(std::uint32_t allowed, vk::MemoryPropertyFlags wanted) const;
	};

	// A window's swapchain and what draws into it, two frames in flight.
	class surface {
	public:
		surface(device& gpu, wl_display* display, wl_surface* surface);
		~surface();

		// What a frame does besides clearing: `prepare` records what has to come before
		// drawing, like uploads, given which of the frames in flight this is; `record`
		// draws.
		struct work {
			std::function<void(const vk::raii::CommandBuffer&, std::size_t slot)> prepare;
			std::function<void(const vk::raii::CommandBuffer&)> record;
		};

		// Draws a frame of `pixels` size, cleared to `background`. Returns false when
		// the swapchain went out of date, which the next frame recreates it for.
		bool draw(one::desktop::size pixels, color background, const work& w = {});
		vk::Format format() const { return format_.format; }
		static constexpr std::size_t in_flight = 2;

	private:
		void make_swapchain(one::desktop::size pixels);

		device& gpu_;
		vk::raii::SurfaceKHR surface_ = nullptr;
		vk::raii::SwapchainKHR swapchain_ = nullptr;
		vk::SurfaceFormatKHR format_{};
		vk::PresentModeKHR present_mode_ = vk::PresentModeKHR::eFifo;
		one::desktop::size extent_{};
		std::vector<vk::Image> images_;
		std::vector<vk::raii::ImageView> views_;
		std::vector<vk::raii::Semaphore> rendered_;  // one per image, signalled when it's drawn

		struct frame {
			vk::raii::CommandBuffer commands = nullptr;
			vk::raii::Semaphore acquired = nullptr;
			vk::raii::Fence done = nullptr;
		};
		vk::raii::CommandPool pool_ = nullptr;
		std::array<frame, in_flight> frames_;
		std::size_t next_ = 0;
		bool stale_ = true;  // the swapchain needs making again
	};

}  // namespace one::desktop::gpu
