// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>

#include "fonts.hpp"

namespace one::desktop {

	namespace {

		// The shaders, compiled to SPIR-V as the library builds.
		alignas(4) constexpr unsigned char box_vertex[] = {
#embed "box.vert.spv"
		};
		alignas(4) constexpr unsigned char box_fragment[] = {
#embed "box.frag.spv"
		};
		alignas(4) constexpr unsigned char glyph_vertex[] = {
#embed "glyph.vert.spv"
		};
		alignas(4) constexpr unsigned char glyph_fragment[] = {
#embed "glyph.frag.spv"
		};
		alignas(4) constexpr unsigned char picture_vertex[] = {
#embed "picture.vert.spv"
		};
		alignas(4) constexpr unsigned char picture_fragment[] = {
#embed "picture.frag.spv"
		};

		// How many pictures may be drawn from at once, each its own descriptor.
		constexpr std::uint32_t most_pictures = 1024;

		vk::raii::ShaderModule module(const vk::raii::Device& device, std::span<const unsigned char> code) {
			return {device, vk::ShaderModuleCreateInfo{.codeSize = code.size(), .pCode = reinterpret_cast<const std::uint32_t*>(code.data())}};
		}

		// A pipeline drawing instanced quads of `stride` bytes, each attribute a vec4,
		// blended as premultiplied color over what's there.
		vk::raii::Pipeline pipeline(const vk::raii::Device& device, const vk::raii::PipelineLayout& layout, vk::Format target,
		                            std::span<const unsigned char> vertex, std::span<const unsigned char> fragment, std::uint32_t attributes) {
			auto vertex_module = module(device, vertex);
			auto fragment_module = module(device, fragment);
			std::array stages{
			    vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eVertex, .module = *vertex_module, .pName = "main"},
			    vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eFragment, .module = *fragment_module, .pName = "main"},
			};
			vk::VertexInputBindingDescription binding{.binding = 0, .stride = attributes * 16, .inputRate = vk::VertexInputRate::eInstance};
			std::vector<vk::VertexInputAttributeDescription> described;
			for (std::uint32_t i = 0; i < attributes; ++i) described.push_back({.location = i, .binding = 0, .format = vk::Format::eR32G32B32A32Sfloat, .offset = i * 16});
			vk::PipelineVertexInputStateCreateInfo input{.vertexBindingDescriptionCount = 1,
			                                             .pVertexBindingDescriptions = &binding,
			                                             .vertexAttributeDescriptionCount = attributes,
			                                             .pVertexAttributeDescriptions = described.data()};
			vk::PipelineInputAssemblyStateCreateInfo assembly{.topology = vk::PrimitiveTopology::eTriangleList};
			vk::PipelineViewportStateCreateInfo viewport{.viewportCount = 1, .scissorCount = 1};
			vk::PipelineRasterizationStateCreateInfo raster{.polygonMode = vk::PolygonMode::eFill, .cullMode = vk::CullModeFlagBits::eNone, .lineWidth = 1};
			vk::PipelineMultisampleStateCreateInfo multisample{.rasterizationSamples = vk::SampleCountFlagBits::e1};
			vk::PipelineColorBlendAttachmentState blend{.blendEnable = true,
			                                            .srcColorBlendFactor = vk::BlendFactor::eOne,
			                                            .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
			                                            .colorBlendOp = vk::BlendOp::eAdd,
			                                            .srcAlphaBlendFactor = vk::BlendFactor::eOne,
			                                            .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
			                                            .alphaBlendOp = vk::BlendOp::eAdd,
			                                            .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
			                                                              vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};
			vk::PipelineColorBlendStateCreateInfo blending{.attachmentCount = 1, .pAttachments = &blend};
			std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
			vk::PipelineDynamicStateCreateInfo dynamic{.dynamicStateCount = dynamic_states.size(), .pDynamicStates = dynamic_states.data()};
			vk::PipelineRenderingCreateInfo rendering{.colorAttachmentCount = 1, .pColorAttachmentFormats = &target};
			return {device, nullptr,
			        vk::GraphicsPipelineCreateInfo{.pNext = &rendering,
			                                       .stageCount = stages.size(),
			                                       .pStages = stages.data(),
			                                       .pVertexInputState = &input,
			                                       .pInputAssemblyState = &assembly,
			                                       .pViewportState = &viewport,
			                                       .pRasterizationState = &raster,
			                                       .pMultisampleState = &multisample,
			                                       .pColorBlendState = &blending,
			                                       .pDynamicState = &dynamic,
			                                       .layout = *layout}};
		}

		void put(float (&to)[4], color c) {
			to[0] = c.red * c.alpha, to[1] = c.green * c.alpha, to[2] = c.blue * c.alpha, to[3] = c.alpha;
		}
		void put(float (&to)[4], const rect& r) { to[0] = r.x, to[1] = r.y, to[2] = r.width, to[3] = r.height; }
		void put_clip(float (&to)[4], const rect& r) { to[0] = r.x, to[1] = r.y, to[2] = r.right(), to[3] = r.bottom(); }

	}  // namespace

	renderer::renderer(gpu::device& gpu, vk::Format target) : gpu_(gpu) {
		const auto& device = gpu.logical;
		vk::PushConstantRange viewport{.stageFlags = vk::ShaderStageFlagBits::eVertex, .offset = 0, .size = 8};
		box_layout_ = vk::raii::PipelineLayout{device, vk::PipelineLayoutCreateInfo{.pushConstantRangeCount = 1, .pPushConstantRanges = &viewport}};
		box_pipeline_ = pipeline(device, box_layout_, target, box_vertex, box_fragment, 5);

		vk::DescriptorSetLayoutBinding sampled{.binding = 0,
		                                       .descriptorType = vk::DescriptorType::eCombinedImageSampler,
		                                       .descriptorCount = 1,
		                                       .stageFlags = vk::ShaderStageFlagBits::eFragment};
		atlas_set_layout_ = vk::raii::DescriptorSetLayout{device, vk::DescriptorSetLayoutCreateInfo{.bindingCount = 1, .pBindings = &sampled}};
		glyph_layout_ = vk::raii::PipelineLayout{device, vk::PipelineLayoutCreateInfo{.setLayoutCount = 1,
		                                                                              .pSetLayouts = &*atlas_set_layout_,
		                                                                              .pushConstantRangeCount = 1,
		                                                                              .pPushConstantRanges = &viewport}};
		glyph_pipeline_ = pipeline(device, glyph_layout_, target, glyph_vertex, glyph_fragment, 4);

		// The atlas: one channel of coverage, sampled exactly, as glyphs land on whole pixels.
		atlas_ = vk::raii::Image{device, vk::ImageCreateInfo{.imageType = vk::ImageType::e2D,
		                                                     .format = vk::Format::eR8Unorm,
		                                                     .extent = {atlas_size, atlas_size, 1},
		                                                     .mipLevels = 1,
		                                                     .arrayLayers = 1,
		                                                     .samples = vk::SampleCountFlagBits::e1,
		                                                     .tiling = vk::ImageTiling::eOptimal,
		                                                     .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst}};
		auto needs = atlas_.getMemoryRequirements();
		atlas_memory_ = vk::raii::DeviceMemory{device, vk::MemoryAllocateInfo{.allocationSize = needs.size,
		                                                                     .memoryTypeIndex = gpu.memory_type(needs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)}};
		atlas_.bindMemory(*atlas_memory_, 0);
		atlas_view_ = vk::raii::ImageView{device, vk::ImageViewCreateInfo{.image = *atlas_,
		                                                                 .viewType = vk::ImageViewType::e2D,
		                                                                 .format = vk::Format::eR8Unorm,
		                                                                 .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}}};
		sampler_ = vk::raii::Sampler{device, vk::SamplerCreateInfo{.magFilter = vk::Filter::eNearest,
		                                                           .minFilter = vk::Filter::eNearest,
		                                                           .addressModeU = vk::SamplerAddressMode::eClampToEdge,
		                                                           .addressModeV = vk::SamplerAddressMode::eClampToEdge}};
		vk::DescriptorPoolSize kind{.type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1};
		atlas_pool_ = vk::raii::DescriptorPool{device, vk::DescriptorPoolCreateInfo{.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		                                                                           .maxSets = 1,
		                                                                           .poolSizeCount = 1,
		                                                                           .pPoolSizes = &kind}};
		auto sets = device.allocateDescriptorSets({.descriptorPool = *atlas_pool_, .descriptorSetCount = 1, .pSetLayouts = &*atlas_set_layout_});
		atlas_set_ = std::move(sets.front());
		vk::DescriptorImageInfo image{.sampler = *sampler_, .imageView = *atlas_view_, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
		device.updateDescriptorSets(vk::WriteDescriptorSet{.dstSet = *atlas_set_,
		                                                   .dstBinding = 0,
		                                                   .descriptorCount = 1,
		                                                   .descriptorType = vk::DescriptorType::eCombinedImageSampler,
		                                                   .pImageInfo = &image},
		                            nullptr);

		// Pictures: drawn with the glyphs' layout, a texture each, sampled smoothly and
		// between their smaller copies when shown smaller, with their corners' radius.
		picture_pipeline_ = pipeline(device, glyph_layout_, target, picture_vertex, picture_fragment, 5);
		smooth_ = vk::raii::Sampler{device, vk::SamplerCreateInfo{.magFilter = vk::Filter::eLinear,
		                                                          .minFilter = vk::Filter::eLinear,
		                                                          .mipmapMode = vk::SamplerMipmapMode::eLinear,
		                                                          .addressModeU = vk::SamplerAddressMode::eClampToEdge,
		                                                          .addressModeV = vk::SamplerAddressMode::eClampToEdge,
		                                                          .maxLod = vk::LodClampNone}};
		vk::DescriptorPoolSize pictures{.type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = most_pictures};
		picture_pool_ = vk::raii::DescriptorPool{device, vk::DescriptorPoolCreateInfo{.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		                                                                             .maxSets = most_pictures,
		                                                                             .poolSizeCount = 1,
		                                                                             .pPoolSizes = &pictures}};
	}

	void renderer::ensure(gpu_buffer& b, vk::DeviceSize wanted, vk::BufferUsageFlags usage) {
		if (b.size >= wanted) return;
		vk::DeviceSize size = std::max<vk::DeviceSize>(wanted, std::max<vk::DeviceSize>(b.size * 2, 64 * 1024));
		b = {};
		b.handle = vk::raii::Buffer{gpu_.logical, vk::BufferCreateInfo{.size = size, .usage = usage, .sharingMode = vk::SharingMode::eExclusive}};
		auto needs = b.handle.getMemoryRequirements();
		b.memory = vk::raii::DeviceMemory{
		    gpu_.logical,
		    vk::MemoryAllocateInfo{.allocationSize = needs.size,
		                           .memoryTypeIndex = gpu_.memory_type(needs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)}};
		b.handle.bindMemory(*b.memory, 0);
		b.mapped = b.memory.mapMemory(0, size);
		b.size = size;
	}

	void renderer::begin(one::desktop::size pixels, double scale, std::size_t slot) {
		pixels_ = pixels;
		scale_ = scale;
		slot_ = slot;
		boxes_.clear();
		glyphs_.clear();
		pictures_.clear();
		batches_.clear();
		// A frame this slot drew before has finished, and so has every frame before it:
		// what was last drawn earlier than that can go.
		++frame_;
		auto finished = [&](std::uint64_t used) { return used + gpu::surface::in_flight < frame_; };
		std::erase_if(retired_, [&](const auto& r) { return finished(r.first); });
		// A texture whose picture is gone, or that nothing has drawn in a long while.
		for (auto it = textures_.begin(); it != textures_.end();) {
			bool gone = it->second.of.expired() || frame_ - it->second.used > 600;
			if (gone && finished(it->second.used)) it = textures_.erase(it);
			else ++it;
		}
	}

	void renderer::add(kind what, const texture* drawn) {
		auto count = static_cast<std::uint32_t>(what == kind::glyphs ? glyphs_.size() : what == kind::pictures ? pictures_.size() : boxes_.size());
		if (!batches_.empty() && batches_.back().what == what && batches_.back().drawn == drawn) {
			++batches_.back().count;
		} else {
			batches_.push_back({what, count - 1, 1, drawn});
		}
	}

	void renderer::box(const rect& area, color fill, color edge, float radius, float border, float blur, const rect& clip) {
		box_instance b{};
		put(b.bounds, area);
		put(b.fill, fill);
		put(b.edge, edge);
		b.shape[0] = radius, b.shape[1] = border, b.shape[2] = blur;
		put_clip(b.clip, clip);
		boxes_.push_back(b);
		add(kind::boxes);
	}

	void renderer::line(point a, point b, float half_width, color tint, const rect& clip) {
		float grow = half_width + 1;
		rect area{std::min(a.x, b.x) - grow, std::min(a.y, b.y) - grow, std::abs(b.x - a.x) + 2 * grow, std::abs(b.y - a.y) + 2 * grow};
		point center{area.x + area.width / 2, area.y + area.height / 2};
		box_instance l{};
		put(l.bounds, area);
		put(l.fill, tint);
		l.edge[0] = a.x - center.x, l.edge[1] = a.y - center.y, l.edge[2] = b.x - center.x, l.edge[3] = b.y - center.y;
		l.shape[1] = half_width, l.shape[3] = 1;
		put_clip(l.clip, clip);
		boxes_.push_back(l);
		add(kind::boxes);
	}

	const renderer::placed& renderer::atlas_entry(const fonts::face* face, std::uint32_t id, int bin) {
		auto key = std::tuple{face, id, bin};
		if (auto it = placed_.find(key); it != placed_.end()) return it->second;
		fonts::bitmap drawn = fonts::draw(*face, id, bin / 4.f);
		auto w = static_cast<std::uint32_t>(drawn.width), h = static_cast<std::uint32_t>(drawn.height);
		placed p{0, 0, 0, 0, drawn.width, drawn.height, drawn.left, drawn.top};
		if (w > 0 && h > 0) {
			// Shelves: along a row while it fits, then a new row under the tallest.
			if (shelf_x_ + w + 1 > atlas_size) shelf_x_ = 0, shelf_y_ += shelf_height_ + 1, shelf_height_ = 0;
			if (shelf_y_ + h + 1 > atlas_size) {
				// Full: everything is drawn again from the top, from the next frame.
				placed_.clear();
				shelf_x_ = shelf_y_ = shelf_height_ = 0;
			}
			p.u0 = static_cast<float>(shelf_x_) / atlas_size, p.v0 = static_cast<float>(shelf_y_) / atlas_size;
			p.u1 = static_cast<float>(shelf_x_ + w) / atlas_size, p.v1 = static_cast<float>(shelf_y_ + h) / atlas_size;
			pending_.push_back({shelf_x_, shelf_y_, w, h, std::move(drawn.coverage)});
			shelf_x_ += w + 1;
			shelf_height_ = std::max(shelf_height_, h);
		}
		return placed_.emplace(key, p).first->second;
	}

	void renderer::glyph(const fonts::face* face, std::uint32_t id, float x, float baseline, color tint, const rect& clip) {
		// Drawn at a quarter pixel's precision across, on whole pixels down.
		float whole = std::floor(x);
		int bin = static_cast<int>(std::lround((x - whole) * 4));
		if (bin == 4) whole += 1, bin = 0;
		const placed& p = atlas_entry(face, id, bin);
		if (p.width == 0) return;
		glyph_instance g{};
		put(g.bounds, rect{whole + p.left, std::round(baseline) - p.top, static_cast<float>(p.width), static_cast<float>(p.height)});
		g.source[0] = p.u0, g.source[1] = p.v0, g.source[2] = p.u1, g.source[3] = p.v1;
		put(g.tint, tint);
		put_clip(g.clip, clip);
		glyphs_.push_back(g);
		add(kind::glyphs);
	}

	renderer::texture* renderer::texture_of(const one::desktop::picture& p) {
		auto shared = p.shared().lock();
		const void* key = shared.get();
		if (auto it = textures_.find(key); it != textures_.end()) {
			if (it->second.of.lock() == shared) return &it->second;
			// Another picture where one that's gone was: the old texture waits out the
			// frames that may still be drawing it.
			retired_.emplace_back(it->second.used, std::move(it->second));
			textures_.erase(it);
		}
		const auto& device = gpu_.logical;
		auto width = static_cast<std::uint32_t>(p.width()), height = static_cast<std::uint32_t>(p.height());
		auto limit = gpu_.physical.getProperties().limits.maxImageDimension2D;
		if (width == 0 || height == 0 || width > limit || height > limit) return nullptr;
		if (textures_.size() >= most_pictures) return nullptr;
		constexpr auto format = vk::Format::eR8G8B8A8Srgb;
		auto features = gpu_.physical.getFormatProperties(format).optimalTilingFeatures;
		bool shrinks = (features & vk::FormatFeatureFlagBits::eBlitSrc) && (features & vk::FormatFeatureFlagBits::eBlitDst) &&
		               (features & vk::FormatFeatureFlagBits::eSampledImageFilterLinear);
		texture t;
		t.of = shared;
		t.width = width, t.height = height;
		t.levels = shrinks ? static_cast<std::uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1 : 1;
		t.image = vk::raii::Image{device, vk::ImageCreateInfo{.imageType = vk::ImageType::e2D,
		                                                      .format = format,
		                                                      .extent = {width, height, 1},
		                                                      .mipLevels = t.levels,
		                                                      .arrayLayers = 1,
		                                                      .samples = vk::SampleCountFlagBits::e1,
		                                                      .tiling = vk::ImageTiling::eOptimal,
		                                                      .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
		                                                               vk::ImageUsageFlagBits::eTransferSrc}};
		auto needs = t.image.getMemoryRequirements();
		t.memory = vk::raii::DeviceMemory{device, vk::MemoryAllocateInfo{.allocationSize = needs.size,
		                                                                 .memoryTypeIndex = gpu_.memory_type(needs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)}};
		t.image.bindMemory(*t.memory, 0);
		t.view = vk::raii::ImageView{device, vk::ImageViewCreateInfo{.image = *t.image,
		                                                            .viewType = vk::ImageViewType::e2D,
		                                                            .format = format,
		                                                            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, t.levels, 0, 1}}};
		auto sets = device.allocateDescriptorSets({.descriptorPool = *picture_pool_, .descriptorSetCount = 1, .pSetLayouts = &*atlas_set_layout_});
		t.set = std::move(sets.front());
		vk::DescriptorImageInfo image{.sampler = *smooth_, .imageView = *t.view, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
		device.updateDescriptorSets(vk::WriteDescriptorSet{.dstSet = *t.set,
		                                                   .dstBinding = 0,
		                                                   .descriptorCount = 1,
		                                                   .descriptorType = vk::DescriptorType::eCombinedImageSampler,
		                                                   .pImageInfo = &image},
		                            nullptr);
		texture* made = &textures_.emplace(key, std::move(t)).first->second;
		waiting_.emplace_back(made, p);
		return made;
	}

	void renderer::picture(const one::desktop::picture& p, const rect& area, const rect& from, float opacity, float radius, const rect& clip) {
		texture* t = texture_of(p);
		if (!t) return;
		t->used = frame_;
		picture_instance g{};
		put(g.bounds, area);
		g.source[0] = from.x, g.source[1] = from.y, g.source[2] = from.right(), g.source[3] = from.bottom();
		put(g.tint, color{1, 1, 1, std::clamp(opacity, 0.f, 1.f)});
		put_clip(g.clip, clip);
		g.shape[0] = std::clamp(radius, 0.f, std::min(area.width, area.height) / 2);
		pictures_.push_back(g);
		add(kind::pictures, t);
	}

	// Each picture made this frame, filled: its pixels copied in, then each smaller
	// copy made from the one before it, half as big, and all of it ready to sample.
	void renderer::upload_pictures(const vk::raii::CommandBuffer& commands) {
		if (waiting_.empty()) return;
		auto& buffers = slots_[slot_];
		std::size_t bytes = 0;
		for (const auto& [t, p] : waiting_) bytes += p.rgba().size();
		ensure(buffers.picture_staging, bytes, vk::BufferUsageFlagBits::eTransferSrc);
		std::size_t offset = 0;
		for (const auto& [t, p] : waiting_) {
			std::memcpy(static_cast<std::uint8_t*>(buffers.picture_staging.mapped) + offset, p.rgba().data(), p.rgba().size());
			auto barrier = [&](std::uint32_t level, std::uint32_t count, vk::ImageLayout from, vk::ImageLayout to, vk::PipelineStageFlags2 after,
			                   vk::AccessFlags2 after_access, vk::PipelineStageFlags2 before, vk::AccessFlags2 before_access) {
				vk::ImageMemoryBarrier2 b{.srcStageMask = after,
				                          .srcAccessMask = after_access,
				                          .dstStageMask = before,
				                          .dstAccessMask = before_access,
				                          .oldLayout = from,
				                          .newLayout = to,
				                          .image = *t->image,
				                          .subresourceRange = {vk::ImageAspectFlagBits::eColor, level, count, 0, 1}};
				commands.pipelineBarrier2(vk::DependencyInfo{.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b});
			};
			using stage = vk::PipelineStageFlagBits2;
			using access = vk::AccessFlagBits2;
			barrier(0, t->levels, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, stage::eNone, {}, stage::eTransfer, access::eTransferWrite);
			commands.copyBufferToImage(*buffers.picture_staging.handle, *t->image, vk::ImageLayout::eTransferDstOptimal,
			                           vk::BufferImageCopy{.bufferOffset = offset,
			                                               .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
			                                               .imageExtent = {t->width, t->height, 1}});
			auto w = static_cast<std::int32_t>(t->width), h = static_cast<std::int32_t>(t->height);
			for (std::uint32_t level = 1; level < t->levels; ++level) {
				barrier(level - 1, 1, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal, stage::eTransfer, access::eTransferWrite,
				        stage::eTransfer, access::eTransferRead);
				std::int32_t half_w = std::max(1, w / 2), half_h = std::max(1, h / 2);
				vk::ImageBlit blit{.srcSubresource = {vk::ImageAspectFlagBits::eColor, level - 1, 0, 1},
				                   .srcOffsets = std::array{vk::Offset3D{0, 0, 0}, vk::Offset3D{w, h, 1}},
				                   .dstSubresource = {vk::ImageAspectFlagBits::eColor, level, 0, 1},
				                   .dstOffsets = std::array{vk::Offset3D{0, 0, 0}, vk::Offset3D{half_w, half_h, 1}}};
				commands.blitImage(*t->image, vk::ImageLayout::eTransferSrcOptimal, *t->image, vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eLinear);
				w = half_w, h = half_h;
			}
			if (t->levels > 1)
				barrier(0, t->levels - 1, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, stage::eTransfer, access::eTransferRead,
				        stage::eFragmentShader, access::eShaderSampledRead);
			barrier(t->levels - 1, 1, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, stage::eTransfer, access::eTransferWrite,
			        stage::eFragmentShader, access::eShaderSampledRead);
			offset += p.rgba().size();
		}
		waiting_.clear();
	}

	void renderer::upload(const vk::raii::CommandBuffer& commands) {
		auto& buffers = slots_[slot_];
		ensure(buffers.boxes, std::max<std::size_t>(1, boxes_.size()) * sizeof(box_instance), vk::BufferUsageFlagBits::eVertexBuffer);
		ensure(buffers.glyphs, std::max<std::size_t>(1, glyphs_.size()) * sizeof(glyph_instance), vk::BufferUsageFlagBits::eVertexBuffer);
		ensure(buffers.pictures, std::max<std::size_t>(1, pictures_.size()) * sizeof(picture_instance), vk::BufferUsageFlagBits::eVertexBuffer);
		std::memcpy(buffers.boxes.mapped, boxes_.data(), boxes_.size() * sizeof(box_instance));
		std::memcpy(buffers.glyphs.mapped, glyphs_.data(), glyphs_.size() * sizeof(glyph_instance));
		std::memcpy(buffers.pictures.mapped, pictures_.data(), pictures_.size() * sizeof(picture_instance));
		upload_pictures(commands);

		vk::ImageSubresourceRange whole{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
		auto barrier = [&](vk::ImageLayout from, vk::ImageLayout to, vk::PipelineStageFlags2 after, vk::AccessFlags2 after_access,
		                   vk::PipelineStageFlags2 before, vk::AccessFlags2 before_access) {
			vk::ImageMemoryBarrier2 b{.srcStageMask = after,
			                          .srcAccessMask = after_access,
			                          .dstStageMask = before,
			                          .dstAccessMask = before_access,
			                          .oldLayout = from,
			                          .newLayout = to,
			                          .image = *atlas_,
			                          .subresourceRange = whole};
			commands.pipelineBarrier2(vk::DependencyInfo{.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b});
		};
		if (!atlas_ready_) {
			// Cleared once, so what no glyph has used yet reads as nothing.
			barrier(vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eNone, {},
			        vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
			commands.clearColorImage(*atlas_, vk::ImageLayout::eTransferDstOptimal, vk::ClearColorValue{std::array{0.f, 0.f, 0.f, 0.f}}, whole);
			barrier(vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eTransfer,
			        vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
			atlas_ready_ = true;
		}
		if (pending_.empty()) return;
		std::size_t bytes = 0;
		for (const auto& p : pending_) bytes += p.coverage.size();
		ensure(buffers.staging, bytes, vk::BufferUsageFlagBits::eTransferSrc);
		std::vector<vk::BufferImageCopy> copies;
		std::size_t offset = 0;
		for (const auto& p : pending_) {
			std::memcpy(static_cast<std::uint8_t*>(buffers.staging.mapped) + offset, p.coverage.data(), p.coverage.size());
			copies.push_back({.bufferOffset = offset,
			                  .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
			                  .imageOffset = {static_cast<std::int32_t>(p.x), static_cast<std::int32_t>(p.y), 0},
			                  .imageExtent = {p.width, p.height, 1}});
			offset += p.coverage.size();
		}
		pending_.clear();
		barrier(vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eFragmentShader,
		        vk::AccessFlagBits2::eShaderSampledRead, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
		commands.copyBufferToImage(*buffers.staging.handle, *atlas_, vk::ImageLayout::eTransferDstOptimal, copies);
		barrier(vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eTransfer,
		        vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
	}

	void renderer::draw(const vk::raii::CommandBuffer& commands) {
		auto& buffers = slots_[slot_];
		std::array viewport{static_cast<float>(pixels_.width), static_cast<float>(pixels_.height)};
		for (const auto& b : batches_) {
			if (b.what == kind::pictures) {
				commands.bindPipeline(vk::PipelineBindPoint::eGraphics, *picture_pipeline_);
				commands.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *glyph_layout_, 0, *b.drawn->set, nullptr);
				commands.pushConstants<float>(*glyph_layout_, vk::ShaderStageFlagBits::eVertex, 0, viewport);
				commands.bindVertexBuffers(0, *buffers.pictures.handle, vk::DeviceSize{0});
			} else if (b.what == kind::glyphs) {
				commands.bindPipeline(vk::PipelineBindPoint::eGraphics, *glyph_pipeline_);
				commands.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *glyph_layout_, 0, *atlas_set_, nullptr);
				commands.pushConstants<float>(*glyph_layout_, vk::ShaderStageFlagBits::eVertex, 0, viewport);
				commands.bindVertexBuffers(0, *buffers.glyphs.handle, vk::DeviceSize{0});
			} else {
				commands.bindPipeline(vk::PipelineBindPoint::eGraphics, *box_pipeline_);
				commands.pushConstants<float>(*box_layout_, vk::ShaderStageFlagBits::eVertex, 0, viewport);
				commands.bindVertexBuffers(0, *buffers.boxes.handle, vk::DeviceSize{0});
			}
			commands.draw(6, b.count, 0, b.first);
		}
	}

	// canvas

	rect rect::intersect(const rect& other) const {
		float left = std::max(x, other.x), top = std::max(y, other.y);
		float r = std::min(right(), other.right()), b = std::min(bottom(), other.bottom());
		return {left, top, std::max(0.f, r - left), std::max(0.f, b - top)};
	}

	one::desktop::size canvas::size() const {
		double s = renderer_.scale();
		return {static_cast<int>(renderer_.pixels().width / s), static_cast<int>(renderer_.pixels().height / s)};
	}
	double canvas::scale() const { return renderer_.scale(); }

	namespace {
		// A logical rectangle in whole pixels, so edges land sharp.
		rect in_pixels(const rect& r, double scale) {
			auto s = static_cast<float>(scale);
			float left = std::round(r.x * s), top = std::round(r.y * s);
			return {left, top, std::round(r.right() * s) - left, std::round(r.bottom() * s) - top};
		}
	}  // namespace

	static rect clip_in_pixels(const std::vector<rect>& clips, const renderer& r) {
		if (clips.empty()) return {0, 0, static_cast<float>(r.pixels().width), static_cast<float>(r.pixels().height)};
		return in_pixels(clips.back(), r.scale());
	}

	void canvas::box(const rect& area, const box_style& style) {
		auto s = static_cast<float>(scale());
		float border = style.border_width > 0 ? std::max(1.f, std::round(style.border_width * s)) : 0;
		renderer_.box(in_pixels(area, scale()), style.fill, style.border, style.radius * s, border, 0, clip_in_pixels(clips_, renderer_));
	}

	void canvas::shadow(const rect& area, float radius, float blur, one::desktop::color tint, point offset) {
		auto s = static_cast<float>(scale());
		rect moved{area.x + offset.x, area.y + offset.y, area.width, area.height};
		renderer_.box(in_pixels(moved, scale()), tint, {0, 0, 0, 0}, radius * s, 0, std::max(0.5f, blur * s), clip_in_pixels(clips_, renderer_));
	}

	void canvas::line(point a, point b, float width, one::desktop::color tint) {
		auto s = static_cast<float>(scale());
		renderer_.line({a.x * s, a.y * s}, {b.x * s, b.y * s}, std::max(0.5f, width * s / 2), tint, clip_in_pixels(clips_, renderer_));
	}

	void canvas::fill(const std::vector<point>& outline, one::desktop::color tint) {
		if (outline.size() < 3) return;
		auto s = static_cast<float>(scale());
		float top = outline[0].y, bottom = outline[0].y;
		for (const auto& p : outline) top = std::min(top, p.y), bottom = std::max(bottom, p.y);
		rect clip = clip_in_pixels(clips_, renderer_);
		// A span of whole pixels for each row it covers, between where its edges cross
		// the row's middle, by the even-odd rule.
		for (float row = std::floor(top * s); row < std::ceil(bottom * s); ++row) {
			float y = (row + 0.5f) / s;
			std::vector<float> crossings;
			for (std::size_t i = 0; i < outline.size(); ++i) {
				point a = outline[i], b = outline[(i + 1) % outline.size()];
				if ((a.y <= y) == (b.y <= y)) continue;
				crossings.push_back(a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x));
			}
			std::sort(crossings.begin(), crossings.end());
			for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) {
				float left = crossings[i] * s, right = crossings[i + 1] * s;
				renderer_.box({left, row, right - left, 1}, tint, {0, 0, 0, 0}, 0, 0, 0, clip);
			}
		}
	}

	void canvas::picture(const one::desktop::picture& p, const rect& area, const rect& from, float opacity, float radius) {
		if (!p) return;
		renderer_.picture(p, in_pixels(area, scale()), from, opacity, radius * static_cast<float>(scale()), clip_in_pixels(clips_, renderer_));
	}

	void canvas::text(const text_layout& laid, point at) {
		auto s = static_cast<float>(scale());
		float x = std::round(at.x * s), y = std::round(at.y * s);
		rect clip = clip_in_pixels(clips_, renderer_);
		for (const auto& g : laid.glyphs()) renderer_.glyph(static_cast<const fonts::face*>(g.face), g.id, x + g.x, y + g.y, g.color, clip);
		for (const auto& rule : laid.rules()) renderer_.box({x + rule.x, y + rule.y, rule.width, rule.thickness}, rule.color, {0, 0, 0, 0}, 0, 0, 0, clip);
	}

	void canvas::finish() {
		auto saved = std::move(clips_);
		clips_.clear();
		for (std::size_t i = 0; i < later_.size(); ++i) later_[i](*this);  // what's drawn later may leave more for later
		later_.clear();
		clips_ = std::move(saved);
	}

	canvas::clip_scope canvas::clip(const rect& area) {
		clips_.push_back(clips_.empty() ? area : clips_.back().intersect(area));
		return clip_scope{*this};
	}

	canvas::clip_scope::~clip_scope() { owner_.clips_.pop_back(); }

}  // namespace one::desktop
