// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

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
#include <vector>

#include "loom/app.hpp"

struct wl_display;
struct wl_surface;

namespace loom::gpu {

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
		// there is one.
		explicit device(wl_display* display);
	};

	// A window's swapchain and what draws into it, two frames in flight.
	class surface {
	public:
		surface(device& gpu, wl_display* display, wl_surface* surface);
		~surface();

		// Draws a frame of `pixels` size: clears to `background`, then lets `record`
		// add its commands. Returns false when the swapchain went out of date, which
		// the next frame recreates it for.
		bool draw(loom::size pixels, color background, const std::function<void(const vk::raii::CommandBuffer&)>& record = {});

	private:
		void make_swapchain(loom::size pixels);

		device& gpu_;
		vk::raii::SurfaceKHR surface_ = nullptr;
		vk::raii::SwapchainKHR swapchain_ = nullptr;
		vk::SurfaceFormatKHR format_{};
		vk::PresentModeKHR present_mode_ = vk::PresentModeKHR::eFifo;
		loom::size extent_{};
		std::vector<vk::Image> images_;
		std::vector<vk::raii::ImageView> views_;
		std::vector<vk::raii::Semaphore> rendered_;  // one per image, signalled when it's drawn
		std::vector<bool> drawn_once_;                // whether an image has been presented, so its layout is known

		struct frame {
			vk::raii::CommandBuffer commands = nullptr;
			vk::raii::Semaphore acquired = nullptr;
			vk::raii::Fence done = nullptr;
		};
		vk::raii::CommandPool pool_ = nullptr;
		std::array<frame, 2> frames_;
		std::size_t next_ = 0;
		bool stale_ = true;  // the swapchain needs making again
	};

}  // namespace loom::gpu
