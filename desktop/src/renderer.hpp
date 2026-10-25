// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What a canvas draws into: boxes, glyphs and pictures as instances, in the order
// they came, drawn with three pipelines; the atlas glyphs are drawn from, filled as
// new ones are needed; and a texture for each picture drawn, made once with its
// smaller copies and kept while it's drawn.

#pragma once

#include <map>
#include <memory>
#include <tuple>
#include <vector>

#include "gpu.hpp"
#include "one/desktop/canvas.hpp"
#include "one/desktop/picture.hpp"

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
		// A segment with round ends, from a to b, in pixels.
		void line(point a, point b, float half_width, color tint, const rect& clip);
		void glyph(const fonts::face* face, std::uint32_t id, float x, float baseline, color tint, const rect& clip);
		// The part of a picture `from` names, as fractions of it, in an area in pixels,
		// as much of it showing as `opacity` says.
		void picture(const one::desktop::picture& p, const rect& area, const rect& from, float opacity, const rect& clip);

	private:
		struct box_instance {
			float bounds[4], fill[4], edge[4], shape[4], clip[4];
		};
		struct glyph_instance {
			float bounds[4], source[4], tint[4], clip[4];
		};
		// A picture's texture: its image with its smaller copies, and the descriptor
		// it's drawn with. Kept while the picture is drawn, then let go once no frame
		// still in flight can be using it.
		struct texture {
			std::weak_ptr<const void> of;
			vk::raii::Image image = nullptr;
			vk::raii::DeviceMemory memory = nullptr;
			vk::raii::ImageView view = nullptr;
			vk::raii::DescriptorSet set = nullptr;
			std::uint32_t width = 0, height = 0, levels = 1;
			std::uint64_t used = 0;  // the last frame it was drawn in
		};
		enum class kind { boxes, glyphs, pictures };
		struct batch {
			kind what;
			std::uint32_t first, count;
			const texture* drawn = nullptr;  // for pictures, whose
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
		texture* texture_of(const one::desktop::picture& p);
		void upload_pictures(const vk::raii::CommandBuffer& commands);
		void ensure(gpu_buffer& b, vk::DeviceSize wanted, vk::BufferUsageFlags usage);
		void add(kind what, const texture* drawn = nullptr);

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

		vk::raii::Pipeline picture_pipeline_ = nullptr;
		vk::raii::Sampler smooth_ = nullptr;
		vk::raii::DescriptorPool picture_pool_ = nullptr;
		std::map<const void*, texture> textures_;
		std::vector<std::pair<std::uint64_t, texture>> retired_;  // let go of, and the frame they were last drawn in
		std::vector<std::pair<texture*, one::desktop::picture>> waiting_;  // made, and to be filled before drawing
		std::uint64_t frame_ = 0;

		struct slot_buffers {
			gpu_buffer boxes, glyphs, pictures, staging, picture_staging;
		};
		std::array<slot_buffers, gpu::surface::in_flight> slots_;
		std::size_t slot_ = 0;
		one::desktop::size pixels_{};
		double scale_ = 1;
		std::vector<box_instance> boxes_;
		std::vector<glyph_instance> glyphs_;
		std::vector<glyph_instance> pictures_;  // laid out as a glyph is: bounds, source, tint, clip
		std::vector<batch> batches_;
	};

}  // namespace one::desktop
