// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

#include "offscreen.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>

#include "gpu.hpp"
#include "renderer.hpp"

namespace loom::offscreen {

	image draw(loom::size size, double scale, color background, const std::function<void(canvas&)>& paint) {
		static gpu::device gpu{nullptr};
		constexpr vk::Format format = vk::Format::eR8G8B8A8Srgb;
		loom::size pixels{static_cast<int>(std::lround(size.width * scale)), static_cast<int>(std::lround(size.height * scale))};
		vk::Extent2D extent{static_cast<std::uint32_t>(pixels.width), static_cast<std::uint32_t>(pixels.height)};
		const auto& device = gpu.logical;

		vk::raii::Image target{device, vk::ImageCreateInfo{.imageType = vk::ImageType::e2D,
		                                                   .format = format,
		                                                   .extent = {extent.width, extent.height, 1},
		                                                   .mipLevels = 1,
		                                                   .arrayLayers = 1,
		                                                   .samples = vk::SampleCountFlagBits::e1,
		                                                   .tiling = vk::ImageTiling::eOptimal,
		                                                   .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc}};
		auto needs = target.getMemoryRequirements();
		vk::raii::DeviceMemory memory{device, vk::MemoryAllocateInfo{.allocationSize = needs.size,
		                                                             .memoryTypeIndex = gpu.memory_type(needs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)}};
		target.bindMemory(*memory, 0);
		vk::raii::ImageView view{device, vk::ImageViewCreateInfo{.image = *target,
		                                                         .viewType = vk::ImageViewType::e2D,
		                                                         .format = format,
		                                                         .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}}};
		gpu_buffer readback;
		vk::DeviceSize bytes = vk::DeviceSize{extent.width} * extent.height * 4;
		readback.handle = vk::raii::Buffer{device, vk::BufferCreateInfo{.size = bytes, .usage = vk::BufferUsageFlagBits::eTransferDst}};
		auto buffer_needs = readback.handle.getMemoryRequirements();
		readback.memory = vk::raii::DeviceMemory{
		    device, vk::MemoryAllocateInfo{.allocationSize = buffer_needs.size,
		                                   .memoryTypeIndex = gpu.memory_type(buffer_needs.memoryTypeBits,
		                                                                      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)}};
		readback.handle.bindMemory(*readback.memory, 0);

		renderer painter{gpu, format};
		painter.begin(pixels, scale, 0);
		canvas c{painter};
		paint(c);

		vk::raii::CommandPool pool{device, vk::CommandPoolCreateInfo{.queueFamilyIndex = gpu.family}};
		auto buffers = device.allocateCommandBuffers({.commandPool = *pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1});
		auto& commands = buffers.front();
		commands.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
		painter.upload(commands);
		auto barrier = [&](vk::ImageLayout from, vk::ImageLayout to, vk::PipelineStageFlags2 after, vk::AccessFlags2 after_access,
		                   vk::PipelineStageFlags2 before, vk::AccessFlags2 before_access) {
			vk::ImageMemoryBarrier2 b{.srcStageMask = after,
			                          .srcAccessMask = after_access,
			                          .dstStageMask = before,
			                          .dstAccessMask = before_access,
			                          .oldLayout = from,
			                          .newLayout = to,
			                          .image = *target,
			                          .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
			commands.pipelineBarrier2(vk::DependencyInfo{.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b});
		};
		barrier(vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal, vk::PipelineStageFlagBits2::eNone, {},
		        vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite);
		vk::RenderingAttachmentInfo attachment{.imageView = *view,
		                                       .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
		                                       .loadOp = vk::AttachmentLoadOp::eClear,
		                                       .storeOp = vk::AttachmentStoreOp::eStore,
		                                       .clearValue = vk::ClearColorValue{std::array{background.red, background.green, background.blue, background.alpha}}};
		commands.beginRendering(vk::RenderingInfo{.renderArea = {{0, 0}, extent}, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &attachment});
		commands.setViewport(0, vk::Viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1});
		commands.setScissor(0, vk::Rect2D{{0, 0}, extent});
		painter.draw(commands);
		commands.endRendering();
		barrier(vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal, vk::PipelineStageFlagBits2::eColorAttachmentOutput,
		        vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead);
		commands.copyImageToBuffer(*target, vk::ImageLayout::eTransferSrcOptimal, *readback.handle,
		                           vk::BufferImageCopy{.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1}, .imageExtent = {extent.width, extent.height, 1}});
		commands.end();
		vk::raii::Fence done{device, vk::FenceCreateInfo{}};
		vk::CommandBufferSubmitInfo submitted{.commandBuffer = *commands};
		gpu.queue.submit2(vk::SubmitInfo2{.commandBufferInfoCount = 1, .pCommandBufferInfos = &submitted}, *done);
		(void)device.waitForFences(*done, true, std::numeric_limits<std::uint64_t>::max());

		image out{pixels.width, pixels.height, std::vector<std::uint8_t>(bytes)};
		void* mapped = readback.memory.mapMemory(0, bytes);
		std::memcpy(out.rgba.data(), mapped, bytes);
		readback.memory.unmapMemory();
		return out;
	}

	namespace {

		std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc = 0) {
			static const auto table = [] {
				std::array<std::uint32_t, 256> t{};
				for (std::uint32_t n = 0; n < 256; ++n) {
					std::uint32_t c = n;
					for (int k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
					t[n] = c;
				}
				return t;
			}();
			crc = ~crc;
			for (std::size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
			return ~crc;
		}

		void big_endian(std::vector<std::uint8_t>& out, std::uint32_t value) {
			for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<std::uint8_t>(value >> shift));
		}

		std::uint32_t read_big_endian(const std::uint8_t* at) {
			return std::uint32_t{at[0]} << 24 | std::uint32_t{at[1]} << 16 | std::uint32_t{at[2]} << 8 | at[3];
		}

		void chunk(std::vector<std::uint8_t>& out, const char* kind, const std::vector<std::uint8_t>& data) {
			big_endian(out, static_cast<std::uint32_t>(data.size()));
			std::size_t start = out.size();
			out.insert(out.end(), kind, kind + 4);
			out.insert(out.end(), data.begin(), data.end());
			big_endian(out, crc32(out.data() + start, out.size() - start));
		}

		constexpr std::array<std::uint8_t, 8> signature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

	}  // namespace

	void write_png(const std::filesystem::path& file, const image& picture) {
		std::vector<std::uint8_t> out(signature.begin(), signature.end());
		std::vector<std::uint8_t> header;
		big_endian(header, static_cast<std::uint32_t>(picture.width));
		big_endian(header, static_cast<std::uint32_t>(picture.height));
		header.insert(header.end(), {8, 6, 0, 0, 0});  // 8 bits, RGBA, deflate, no filter, not interlaced
		chunk(out, "IHDR", header);

		// Each row after a 0, no filter; then zlib's stored blocks, and its checksum.
		std::vector<std::uint8_t> rows;
		std::size_t stride = static_cast<std::size_t>(picture.width) * 4;
		for (int y = 0; y < picture.height; ++y) {
			rows.push_back(0);
			rows.insert(rows.end(), picture.rgba.begin() + static_cast<std::ptrdiff_t>(y * stride), picture.rgba.begin() + static_cast<std::ptrdiff_t>((y + 1) * stride));
		}
		std::vector<std::uint8_t> deflated{0x78, 0x01};
		for (std::size_t at = 0; at < rows.size() || at == 0; at += 65535) {
			std::size_t length = std::min<std::size_t>(65535, rows.size() - at);
			deflated.push_back(at + length >= rows.size() ? 1 : 0);
			deflated.push_back(static_cast<std::uint8_t>(length));
			deflated.push_back(static_cast<std::uint8_t>(length >> 8));
			deflated.push_back(static_cast<std::uint8_t>(~length));
			deflated.push_back(static_cast<std::uint8_t>(~length >> 8));
			deflated.insert(deflated.end(), rows.begin() + static_cast<std::ptrdiff_t>(at), rows.begin() + static_cast<std::ptrdiff_t>(at + length));
			if (rows.empty()) break;
		}
		std::uint32_t a = 1, b = 0;
		for (auto byte : rows) a = (a + byte) % 65521, b = (b + a) % 65521;
		big_endian(deflated, b << 16 | a);
		chunk(out, "IDAT", deflated);
		chunk(out, "IEND", {});
		std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
	}

	std::optional<image> read_png(const std::filesystem::path& file) {
		std::ifstream in(file, std::ios::binary);
		if (!in) return std::nullopt;
		std::vector<std::uint8_t> data{std::istreambuf_iterator<char>(in), {}};
		if (data.size() < 8 || !std::equal(signature.begin(), signature.end(), data.begin())) return std::nullopt;
		image out;
		std::vector<std::uint8_t> deflated;
		for (std::size_t at = 8; at + 12 <= data.size();) {
			std::uint32_t length = read_big_endian(&data[at]);
			std::string kind(reinterpret_cast<const char*>(&data[at + 4]), 4);
			const std::uint8_t* body = &data[at + 8];
			if (kind == "IHDR") {
				out.width = static_cast<int>(read_big_endian(body));
				out.height = static_cast<int>(read_big_endian(body + 4));
				if (body[8] != 8 || body[9] != 6) return std::nullopt;  // only what write_png writes
			} else if (kind == "IDAT") {
				deflated.insert(deflated.end(), body, body + length);
			}
			at += 12 + length;
		}
		// Stored blocks only, as written above.
		std::vector<std::uint8_t> rows;
		for (std::size_t at = 2; at + 5 <= deflated.size();) {
			bool last = deflated[at] & 1;
			if ((deflated[at] >> 1) & 3) return std::nullopt;
			std::size_t length = deflated[at + 1] | deflated[at + 2] << 8;
			rows.insert(rows.end(), deflated.begin() + static_cast<std::ptrdiff_t>(at + 5), deflated.begin() + static_cast<std::ptrdiff_t>(at + 5 + length));
			at += 5 + length;
			if (last) break;
		}
		std::size_t stride = static_cast<std::size_t>(out.width) * 4;
		if (rows.size() != (stride + 1) * out.height) return std::nullopt;
		for (int y = 0; y < out.height; ++y) {
			auto row = rows.begin() + static_cast<std::ptrdiff_t>(y * (stride + 1) + 1);
			out.rgba.insert(out.rgba.end(), row, row + static_cast<std::ptrdiff_t>(stride));
		}
		return out;
	}

	std::size_t differences(const image& a, const image& b, int tolerance) {
		if (a.width != b.width || a.height != b.height) return std::numeric_limits<std::size_t>::max();
		std::size_t count = 0;
		for (std::size_t i = 0; i < a.rgba.size(); i += 4) {
			for (int k = 0; k < 4; ++k) {
				if (std::abs(int{a.rgba[i + k]} - int{b.rgba[i + k]}) > tolerance) {
					++count;
					break;
				}
			}
		}
		return count;
	}

}  // namespace loom::offscreen
