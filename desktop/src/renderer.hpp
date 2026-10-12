// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What a canvas draws into: boxes and glyphs as instances, in the order they came,
// drawn with two pipelines, and the atlas glyphs are drawn from, filled as new ones
// are needed.

#pragma once

#include <map>
#include <tuple>
#include <vector>

#include "gpu.hpp"
#include "one/desktop/canvas.hpp"

namespace one::desktop {

	namespace fonts {
		struct face;
	}

	// A buffer and its memory, mapped when the CPU writes it.
	struct gpu_buffer {
		vk::raii::Buffer handle = nullptr;
		vk::raii::DeviceMemory memory = nullptr;
		void* mapped = nullptr;
		vk::DeviceSize size = 0;
	};

	class renderer {
	public:
		renderer(gpu::device& gpu, vk::Format target);

		// A frame: begun with its size and the frame-in-flight slot whose buffers it
		// may write, filled through a canvas, then recorded.
		void begin(one::desktop::size pixels, double scale, std::size_t slot);
		void upload(const vk::raii::CommandBuffer& commands);  // before rendering
		void draw(const vk::raii::CommandBuffer& commands);    // within it

		one::desktop::size pixels() const { return pixels_; }
		double scale() const { return scale_; }

		// In pixels, premultiplied, within the clip given in pixels.
		void box(const rect& area, color fill, color edge, float radius, float border, float blur, const rect& clip);
		void glyph(const fonts::face* face, std::uint32_t id, float x, float baseline, color tint, const rect& clip);

	private:
		struct box_instance {
			float bounds[4], fill[4], edge[4], shape[4], clip[4];
		};
		struct glyph_instance {
			float bounds[4], source[4], tint[4], clip[4];
		};
		struct batch {
			bool glyphs;
			std::uint32_t first, count;
		};
		struct placed {
			float u0, v0, u1, v1;
			int width, height, left, top;
		};
		struct pending {
			std::uint32_t x, y, width, height;
			std::vector<std::uint8_t> coverage;
		};

		const placed& atlas_entry(const fonts::face* face, std::uint32_t id, int bin);
		void ensure(gpu_buffer& b, vk::DeviceSize wanted, vk::BufferUsageFlags usage);
		void add(bool glyphs);

		gpu::device& gpu_;
		vk::raii::PipelineLayout box_layout_ = nullptr;
		vk::raii::Pipeline box_pipeline_ = nullptr;
		vk::raii::DescriptorSetLayout atlas_set_layout_ = nullptr;
		vk::raii::DescriptorPool atlas_pool_ = nullptr;
		vk::raii::DescriptorSet atlas_set_ = nullptr;
		vk::raii::PipelineLayout glyph_layout_ = nullptr;
		vk::raii::Pipeline glyph_pipeline_ = nullptr;

		static constexpr std::uint32_t atlas_size = 2048;
		vk::raii::Image atlas_ = nullptr;
		vk::raii::DeviceMemory atlas_memory_ = nullptr;
		vk::raii::ImageView atlas_view_ = nullptr;
		vk::raii::Sampler sampler_ = nullptr;
		bool atlas_ready_ = false;
		std::uint32_t shelf_x_ = 0, shelf_y_ = 0, shelf_height_ = 0;
		std::map<std::tuple<const fonts::face*, std::uint32_t, int>, placed> placed_;
		std::vector<pending> pending_;

		struct slot_buffers {
			gpu_buffer boxes, glyphs, staging;
		};
		std::array<slot_buffers, gpu::surface::in_flight> slots_;
		std::size_t slot_ = 0;
		one::desktop::size pixels_{};
		double scale_ = 1;
		std::vector<box_instance> boxes_;
		std::vector<glyph_instance> glyphs_;
		std::vector<batch> batches_;
	};

}  // namespace one::desktop
