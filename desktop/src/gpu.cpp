// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

#include "gpu.hpp"

#include <algorithm>
#include <limits>
#include <ranges>
#include <stdexcept>

namespace one::desktop::gpu {

	device::device(wl_display* display) {
		vk::ApplicationInfo application{.pApplicationName = "uione", .pEngineName = "one::desktop", .apiVersion = vk::ApiVersion13};
		std::array<const char*, 2> extensions{vk::KHRSurfaceExtensionName, vk::KHRWaylandSurfaceExtensionName};
		instance = vk::raii::Instance{context, vk::InstanceCreateInfo{.pApplicationInfo = &application,
		                                                              .enabledExtensionCount = extensions.size(),
		                                                              .ppEnabledExtensionNames = extensions.data()}};

		// A GPU that does Vulkan 1.3, presents to Wayland from one of its queues that
		// also draws; discrete first.
		std::optional<std::pair<vk::raii::PhysicalDevice, std::uint32_t>> chosen;
		bool chosen_discrete = false;
		for (auto& candidate : instance.enumeratePhysicalDevices()) {
			auto properties = candidate.getProperties();
			if (properties.apiVersion < vk::ApiVersion13) continue;
			auto families = candidate.getQueueFamilyProperties();
			for (std::uint32_t i = 0; i < families.size(); ++i) {
				bool draws = static_cast<bool>(families[i].queueFlags & vk::QueueFlagBits::eGraphics);
				if (!draws || (display && !candidate.getWaylandPresentationSupportKHR(i, *display))) continue;
				bool discrete = properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu;
				if (!chosen || (discrete && !chosen_discrete)) {
					chosen.emplace(candidate, i);
					chosen_discrete = discrete;
				}
				break;
			}
		}
		if (!chosen) throw std::runtime_error(display ? "no GPU here draws with Vulkan 1.3 on Wayland" : "no GPU here draws with Vulkan 1.3");
		physical = std::move(chosen->first);
		family = chosen->second;
		name = physical.getProperties().deviceName.data();

		float priority = 1;
		vk::DeviceQueueCreateInfo queue_info{.queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
		vk::PhysicalDeviceVulkan13Features modern{.synchronization2 = true, .dynamicRendering = true};
		std::array<const char*, 1> device_extensions{vk::KHRSwapchainExtensionName};
		logical = vk::raii::Device{physical, vk::DeviceCreateInfo{.pNext = &modern,
		                                                          .queueCreateInfoCount = 1,
		                                                          .pQueueCreateInfos = &queue_info,
		                                                          .enabledExtensionCount = device_extensions.size(),
		                                                          .ppEnabledExtensionNames = device_extensions.data()}};
		queue = logical.getQueue(family, 0);
	}

	std::uint32_t device::memory_type(std::uint32_t allowed, vk::MemoryPropertyFlags wanted) const {
		auto kinds = physical.getMemoryProperties();
		for (std::uint32_t i = 0; i < kinds.memoryTypeCount; ++i) {
			if ((allowed & (1u << i)) && (kinds.memoryTypes[i].propertyFlags & wanted) == wanted) return i;
		}
		throw std::runtime_error("the GPU has no memory of the kind asked for");
	}

	surface::surface(device& gpu, wl_display* display, wl_surface* surface) : gpu_(gpu) {
		surface_ = vk::raii::SurfaceKHR{gpu.instance, vk::WaylandSurfaceCreateInfoKHR{.display = display, .surface = surface}};
		// sRGB, so blending happens in linear light and colors come out as written.
		auto formats = gpu.physical.getSurfaceFormatsKHR(*surface_);
		format_ = formats.front();
		for (const auto& f : formats) {
			if ((f.format == vk::Format::eB8G8R8A8Srgb || f.format == vk::Format::eR8G8B8A8Srgb) && f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
				format_ = f;
				break;
			}
		}
		// Mailbox never blocks on a hidden window; the compositor's frame callbacks pace
		// drawing instead.
		auto modes = gpu.physical.getSurfacePresentModesKHR(*surface_);
		if (std::ranges::contains(modes, vk::PresentModeKHR::eMailbox)) present_mode_ = vk::PresentModeKHR::eMailbox;

		pool_ = vk::raii::CommandPool{gpu.logical, vk::CommandPoolCreateInfo{.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
		                                                                     .queueFamilyIndex = gpu.family}};
		auto buffers = gpu.logical.allocateCommandBuffers({.commandPool = *pool_, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = frames_.size()});
		for (std::size_t i = 0; i < frames_.size(); ++i) {
			frames_[i].commands = std::move(buffers[i]);
			frames_[i].acquired = vk::raii::Semaphore{gpu.logical, vk::SemaphoreCreateInfo{}};
			frames_[i].done = vk::raii::Fence{gpu.logical, vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled}};
		}
	}

	surface::~surface() { gpu_.logical.waitIdle(); }

	void surface::make_swapchain(one::desktop::size pixels) {
		gpu_.logical.waitIdle();
		auto capabilities = gpu_.physical.getSurfaceCapabilitiesKHR(*surface_);
		std::uint32_t count = std::max(capabilities.minImageCount, 3u);
		if (capabilities.maxImageCount) count = std::min(count, capabilities.maxImageCount);
		vk::Extent2D extent{static_cast<std::uint32_t>(pixels.width), static_cast<std::uint32_t>(pixels.height)};
		// On Wayland the surface takes the swapchain's size, unless the driver says one.
		if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) extent = capabilities.currentExtent;
		extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
		extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

		vk::SwapchainKHR old = swapchain_ == nullptr ? vk::SwapchainKHR{} : *swapchain_;
		vk::raii::SwapchainKHR made{gpu_.logical, vk::SwapchainCreateInfoKHR{
		                                              .surface = *surface_,
		                                              .minImageCount = count,
		                                              .imageFormat = format_.format,
		                                              .imageColorSpace = format_.colorSpace,
		                                              .imageExtent = extent,
		                                              .imageArrayLayers = 1,
		                                              .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
		                                              .imageSharingMode = vk::SharingMode::eExclusive,
		                                              .preTransform = capabilities.currentTransform,
		                                              .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
		                                              .presentMode = present_mode_,
		                                              .clipped = true,
		                                              .oldSwapchain = old,
		                                          }};
		views_.clear();
		rendered_.clear();
		swapchain_ = std::move(made);
		images_ = swapchain_.getImages();
		for (auto image : images_) {
			views_.emplace_back(gpu_.logical, vk::ImageViewCreateInfo{.image = image,
			                                                          .viewType = vk::ImageViewType::e2D,
			                                                          .format = format_.format,
			                                                          .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}});
			rendered_.emplace_back(gpu_.logical, vk::SemaphoreCreateInfo{});
		}
		extent_ = {static_cast<int>(extent.width), static_cast<int>(extent.height)};
		stale_ = false;
	}

	// Moves an image between layouts, waiting for what came before.
	static void transition(const vk::raii::CommandBuffer& commands, vk::Image image, vk::ImageLayout from, vk::ImageLayout to,
	                       vk::PipelineStageFlags2 after, vk::AccessFlags2 after_access, vk::PipelineStageFlags2 before, vk::AccessFlags2 before_access) {
		vk::ImageMemoryBarrier2 barrier{.srcStageMask = after,
		                                .srcAccessMask = after_access,
		                                .dstStageMask = before,
		                                .dstAccessMask = before_access,
		                                .oldLayout = from,
		                                .newLayout = to,
		                                .image = image,
		                                .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
		commands.pipelineBarrier2(vk::DependencyInfo{.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
	}

	bool surface::draw(one::desktop::size pixels, color background, const work& w) {
		if (stale_ || pixels != extent_) make_swapchain(pixels);
		std::size_t slot = next_;
		frame& f = frames_[slot];
		next_ = (next_ + 1) % frames_.size();
		(void)gpu_.logical.waitForFences(*f.done, true, std::numeric_limits<std::uint64_t>::max());

		std::uint32_t index = 0;
		try {
			auto [result, acquired] = swapchain_.acquireNextImage(std::numeric_limits<std::uint64_t>::max(), *f.acquired);
			if (result == vk::Result::eSuboptimalKHR) stale_ = true;
			index = acquired;
		} catch (const vk::OutOfDateKHRError&) {
			stale_ = true;
			return false;
		}
		gpu_.logical.resetFences(*f.done);

		const auto& commands = f.commands;
		commands.reset();
		commands.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
		if (w.prepare) w.prepare(commands, slot);
		transition(commands, images_[index], vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
		           vk::PipelineStageFlagBits2::eColorAttachmentOutput, {}, vk::PipelineStageFlagBits2::eColorAttachmentOutput,
		           vk::AccessFlagBits2::eColorAttachmentWrite);
		vk::RenderingAttachmentInfo target{.imageView = *views_[index],
		                                   .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
		                                   .loadOp = vk::AttachmentLoadOp::eClear,
		                                   .storeOp = vk::AttachmentStoreOp::eStore,
		                                   .clearValue = vk::ClearColorValue{std::array{background.red, background.green, background.blue, background.alpha}}};
		vk::Extent2D extent{static_cast<std::uint32_t>(extent_.width), static_cast<std::uint32_t>(extent_.height)};
		commands.beginRendering(vk::RenderingInfo{.renderArea = {{0, 0}, extent}, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &target});
		commands.setViewport(0, vk::Viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1});
		commands.setScissor(0, vk::Rect2D{{0, 0}, extent});
		if (w.record) w.record(commands);
		commands.endRendering();
		transition(commands, images_[index], vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
		           vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite,
		           vk::PipelineStageFlagBits2::eBottomOfPipe, {});
		commands.end();

		vk::SemaphoreSubmitInfo wait{.semaphore = *f.acquired, .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput};
		vk::SemaphoreSubmitInfo signal{.semaphore = *rendered_[index], .stageMask = vk::PipelineStageFlagBits2::eAllGraphics};
		vk::CommandBufferSubmitInfo submitted{.commandBuffer = *commands};
		gpu_.queue.submit2(vk::SubmitInfo2{.waitSemaphoreInfoCount = 1,
		                                   .pWaitSemaphoreInfos = &wait,
		                                   .commandBufferInfoCount = 1,
		                                   .pCommandBufferInfos = &submitted,
		                                   .signalSemaphoreInfoCount = 1,
		                                   .pSignalSemaphoreInfos = &signal},
		                   *f.done);
		vk::SwapchainKHR chain = *swapchain_;
		vk::Semaphore rendered = *rendered_[index];
		try {
			auto result = gpu_.queue.presentKHR(vk::PresentInfoKHR{.waitSemaphoreCount = 1,
			                                                       .pWaitSemaphores = &rendered,
			                                                       .swapchainCount = 1,
			                                                       .pSwapchains = &chain,
			                                                       .pImageIndices = &index});
			if (result == vk::Result::eSuboptimalKHR) stale_ = true;
		} catch (const vk::OutOfDateKHRError&) {
			stale_ = true;
		}
		return true;
	}

}  // namespace one::desktop::gpu
