// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A collage: a picture made of layers, as big as it says, drawn scaled to fit on the
// workspace with room around it, or at its own size on black filling the screen
// (decision 0021). Pictures are cropped, turned and mirrored as each layer says, the
// rest of the collage filled around one from its edges or its edge color as it says,
// or, harmonized, what's under them all filled from theirs together, each melting into
// it; words are drawn over a soft shadow. A click picks a layer, framed with its corners and the buttons over it; a
// picked layer is moved, or resized by a corner with its shape kept unless Shift is
// held, snapped unless Alt is. A picture is cropped in place,
// its whole picture faint around what's kept, and words are written where they are,
// with a caret, and resized by their size. Where everything goes is one::screens's
// collage model, the web's too.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <limits>
#include <mutex>
#include <optional>
#include <set>

#include <one/screens/collage.hpp>

#include <unistd.h>

#include <fstream>
#include <thread>

#include <ember.hpp>

#include "one/desktop/runtime/components.hpp"
#include "one/desktop/files.hpp"
#include "one/desktop/screenshot.hpp"

namespace one::desktop::runtime {

	namespace {

		namespace model = screens::collage;
		using fields = std::map<std::string, value>;

		constexpr float handle = 10;  // a corner's handle, in the window
		constexpr float unwrapped = 1e7f;

		model::box box_of(const value& layer) { return {layer["x"].as_double(), layer["y"].as_double(), layer["w"].as_double(), layer["h"].as_double()}; }

		model::box crop_of(const value& layer) {
			auto or_else = [&](const char* f, double otherwise) { return layer[f].is_null() ? otherwise : layer[f].as_double(); };
			return {or_else("crop_x", 0), or_else("crop_y", 0), or_else("crop_w", 1), or_else("crop_h", 1)};
		}

		// What a layer is: a picture, when it has one; words, when it has those; or else a
		// shape, as its parts say (decision 0025).
		bool is_picture(const value& layer) { return !layer["picture"].as_string().empty(); }
		bool is_words(const value& layer) { return !is_picture(layer) && !layer["words"].as_string().empty(); }
		bool is_shape(const value& layer) { return !is_picture(layer) && !is_words(layer); }

		bool is_path(const value& layer) { return is_shape(layer) && layer["shape"].as_string() == "path"; }

		// A path's outline where it is, in the collage's own units, its box `b`.
		std::string outline_of(const value& layer, model::box b) { return model::path_in(layer["points"].as_string(), b.x, b.y, b.w, b.h); }

		// Whether a press at `at`, in the collage's units, reaches a layer, its box `b`: a
		// path by its stroke, 6 window pixels around it and half its width, or inside it
		// while it's filled; anything else within its box. The same as the web's.
		bool hits(const value& layer, model::box b, model::point at, double per_pixel) {
			if (!is_path(layer)) return at.x >= b.x && at.x <= b.x + b.w && at.y >= b.y && at.y <= b.y + b.h;
			double reach = 6 * per_pixel + (layer["stroke"].as_string().empty() ? 0 : layer["stroke_width"].as_double() / 2);
			std::string fill = layer["fill"].as_string();
			return model::path_reaches(outline_of(layer, b), at, reach, fill.size() == 7 && fill[0] == '#');
		}

		// A path reshaped, as its points and its box: its box a pixel wide where it's
		// straight across or down, as a line drawn is.
		std::pair<std::string, model::box> reshaped_as(const std::string& outline) {
			model::box b = model::path_bounds(outline);
			if (b.w < 1) b.x += b.w / 2 - 0.5, b.w = 1;
			if (b.h < 1) b.y += b.h / 2 - 0.5, b.h = 1;
			return {model::path_fractions(outline, b), b};
		}

		// How much of a layer shows: all of it, unless its opacity says less.
		float opacity_of(const value& layer) { return layer["opacity"].is_null() ? 1.f : std::clamp(static_cast<float>(layer["opacity"].as_double()), 0.f, 1.f); }

		desktop::rect as_rect(const model::box& b) { return {static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.w), static_cast<float>(b.h)}; }

		desktop::color color_of(const std::string& written, desktop::color otherwise) {
			if (written.size() != 7 || written[0] != '#') return otherwise;
			char* end = nullptr;
			unsigned long hex = std::strtoul(written.c_str() + 1, &end, 16);
			return end && *end == 0 ? desktop::color::hex(static_cast<std::uint32_t>(hex)) : otherwise;
		}

		// What's worked out from pictures, kept by what it's made of, for any thread: made
		// outside the lock, so nothing waits on another's work, and handed out whole, its
		// pixels shared, so letting older ones go never pulls one from under a drawing.
		template <class T>
		class kept_by {
		public:
			explicit kept_by(std::size_t most) : most_(most) {}
			std::optional<T> peek(const std::string& key) {
				std::lock_guard hold(lock_);
				auto found = made_.find(key);
				return found == made_.end() ? std::nullopt : std::optional<T>(found->second);
			}
			template <class Make>
			T get(const std::string& key, Make make) {
				if (auto known = peek(key)) return *known;
				T fresh = make();
				std::lock_guard hold(lock_);
				if (made_.size() >= most_) made_.clear();
				return made_.emplace(key, std::move(fresh)).first->second;
			}

		private:
			std::mutex lock_;
			std::map<std::string, T> made_;
			std::size_t most_;
		};

		// An SVG's text, read once, by where it's kept; nothing when the file isn't one.
		// An SVG layer is drawn from its vectors at the size it's shown, never read as
		// pixels.
		std::string vector_of(const std::filesystem::path& file) {
			static kept_by<std::string> read(std::numeric_limits<std::size_t>::max());
			return read.get(file.string(), [&] {
				std::ifstream in(file, std::ios::binary);
				std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				return desktop::picture::is_svg({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()}) ? text : std::string();
			});
		}
		std::string vector_here(page& p, const value& layer) {
			if (!is_picture(layer)) return {};
			auto file = p.file_here(layer["picture"].as_string());
			return file ? vector_of(*file) : std::string();
		}

		// Each picture read once, by where it's kept.
		desktop::picture picture_kept(const std::filesystem::path& file) {
			static kept_by<desktop::picture> read(std::numeric_limits<std::size_t>::max());
			return read.get(file.string(), [&] {
				auto p = desktop::picture::read(file);
				return p ? *p : desktop::picture();
			});
		}

		// What's drawn in the window from a picture kept at `file`: the whole of it, or a
		// copy no longer on its longer side than `longest`, made once and kept beside it,
		// by its hash and that size, where it's named by its hash. Read away from the
		// window's thread, and nothing until it is; the window built again then. With no
		// window to build again, it's read now.
		state<int> pictures_read{0};
		kept_by<desktop::picture>& pictures_kept(int longest) {
			static kept_by<desktop::picture> whole(8), small(512);
			return longest > 0 ? small : whole;
		}
		// The whole of one `wanted` only while something holds it, like the page it's on, is
		// read in turn with the others wanted so, one at a time, the last asked for first,
		// and not at all once nothing holds it: so stepping on through records faster than
		// their pictures can be read reads the one shown, not each one passed.
		struct waiting_read {
			std::string key;
			std::function<desktop::picture()> make;
			std::weak_ptr<void> wanted;
		};
		std::mutex reading;
		std::set<std::string> working;
		std::vector<waiting_read> waiting;
		bool reading_in_turn = false;
		void read_in_turn() {
			for (;;) {
				waiting_read next;
				{
					std::lock_guard hold(reading);
					std::erase_if(waiting, [](const waiting_read& w) { return w.wanted.expired() && working.erase(w.key); });
					if (waiting.empty()) {
						reading_in_turn = false;
						break;
					}
					next = std::move(waiting.back());
					waiting.pop_back();
				}
				pictures_kept(0).get(next.key, next.make);
				{
					std::lock_guard hold(reading);
					working.erase(next.key);
				}
				post([] {
					pictures_read.set(pictures_read.peek() + 1);
					redraw();
				});
			}
		}
		std::optional<desktop::picture> read_soon(const std::filesystem::path& file, int longest, std::optional<std::weak_ptr<void>> wanted = std::nullopt) {
			auto& kept = pictures_kept(longest);
			std::string key = std::format("{} {}", file.string(), longest);
			auto make = [file, longest] {
				if (longest <= 0) return desktop::picture::read(file).value_or(desktop::picture());
				std::string name = file.filename().string();
				bool hashed = name.size() == 64 && name.find_first_not_of("0123456789abcdef") == std::string::npos;
				// One turned by its camera is kept by another name, so a copy made before
				// pictures were turned upright isn't drawn on its side.
				bool turned = hashed && desktop::facts_of(file).orientation != 1;
				std::filesystem::path beside = file.parent_path() / "small" / std::format("{}-{}{}.png", name, longest, turned ? "-upright" : "");
				if (hashed) {
					if (auto read = desktop::picture::read(beside)) return *read;
				}
				auto read = desktop::picture::read(file);
				if (!read) return desktop::picture();
				desktop::picture made = read->smaller(longest);
				std::error_code failed;
				if (hashed && made.width() < read->width() && (std::filesystem::create_directories(beside.parent_path(), failed), !failed)) {
					std::filesystem::path writing = beside;
					writing += std::format(".{}", ::getpid());
					if (made.write_png(writing)) std::filesystem::rename(writing, beside, failed);
				}
				return made;
			};
			if (!posting()) return kept.get(key, make);
			if (auto known = kept.peek(key)) return known;
			pictures_read.get();
			std::lock_guard hold(reading);
			if (longest <= 0 && wanted) {
				// Asked for again, it's the last asked for, by what asked.
				auto found = std::ranges::find(waiting, key, &waiting_read::key);
				if (found != waiting.end()) {
					found->wanted = *wanted;
					std::rotate(found, found + 1, waiting.end());
				} else if (working.insert(key).second) {
					waiting.push_back({key, make, *wanted});
				}
				if (!std::exchange(reading_in_turn, true)) in_background(read_in_turn);
				return std::nullopt;
			}
			if (working.insert(key).second) {
				in_background([key, make, &kept] {
					kept.get(key, make);
					{
						std::lock_guard hold(reading);
						working.erase(key);
					}
					post([] {
						pictures_read.set(pictures_read.peek() + 1);
						redraw();
					});
				});
			}
			return std::nullopt;
		}

		// A picture mirrored, then turned a quarter clockwise as many times as `turns`.
		desktop::picture oriented(const desktop::picture& p, int turns, bool mirrored) {
			int w = p.width(), h = p.height();
			std::vector<std::uint8_t> pixels = p.rgba();
			auto at = [](std::vector<std::uint8_t>& in, int width, int x, int y) { return in.begin() + (static_cast<std::ptrdiff_t>(y) * width + x) * 4; };
			if (mirrored) {
				for (int y = 0; y < h; ++y) {
					for (int x = 0; x < w / 2; ++x) std::swap_ranges(at(pixels, w, x, y), at(pixels, w, x, y) + 4, at(pixels, w, w - 1 - x, y));
				}
			}
			for (int turn = 0; turn < turns; ++turn) {
				// Each pixel of the turned picture is the one below it, before.
				std::vector<std::uint8_t> out(pixels.size());
				for (int y = 0; y < w; ++y) {
					for (int x = 0; x < h; ++x) std::copy_n(at(pixels, w, y, h - 1 - x), 4, at(out, h, x, y));
				}
				pixels = std::move(out);
				std::swap(w, h);
			}
			return desktop::picture::of(w, h, std::move(pixels));
		}

		// A picture kept at `file`, turned and mirrored as a layer says.
		desktop::picture turned_at(const std::filesystem::path& file, const value& layer) {
			int turns = (static_cast<int>(layer["turns"].as_double()) % 4 + 4) % 4;
			bool mirrored = layer["mirrored"].as_double() != 0;
			desktop::picture whole = picture_kept(file);
			if (!whole || (turns == 0 && !mirrored)) return whole;
			static kept_by<desktop::picture> made(64);
			return made.get(std::format("{} {} {}", file.string(), turns, mirrored), [&] { return oriented(whole, turns, mirrored); });
		}

		// A layer's picture as it's shown: turned and mirrored as the layer says.
		desktop::picture layer_picture(page& p, const value& layer) {
			auto file = p.file_here(layer["picture"].as_string());
			return file ? turned_at(*file, layer) : desktop::picture();
		}

		// The part `crop` keeps of a picture made `w` by `h`, each of its cells the average
		// of the pixels it covers, three channels a cell: worked out on the processor, so
		// it's the same on any thread and while anything else is being drawn.
		std::vector<double> averaged(const desktop::picture& p, model::box crop, int w, int h) {
			std::vector<double> out(static_cast<std::size_t>(w) * h * 3, 0);
			const auto& rgba = p.rgba();
			int pw = p.width(), ph = p.height();
			if (pw <= 0 || ph <= 0) return out;
			for (int y = 0; y < h; ++y) {
				int top = std::clamp(static_cast<int>((crop.y + crop.h * y / h) * ph), 0, ph - 1);
				int bottom = std::clamp(static_cast<int>(std::ceil((crop.y + crop.h * (y + 1) / h) * ph)), top + 1, ph);
				for (int x = 0; x < w; ++x) {
					int left = std::clamp(static_cast<int>((crop.x + crop.w * x / w) * pw), 0, pw - 1);
					int right = std::clamp(static_cast<int>(std::ceil((crop.x + crop.w * (x + 1) / w) * pw)), left + 1, pw);
					double sum[3] = {0, 0, 0};
					for (int py = top; py < bottom; ++py) {
						for (int px = left; px < right; ++px) {
							for (int c = 0; c < 3; ++c) sum[c] += rgba[(static_cast<std::size_t>(py) * pw + px) * 4 + c];
						}
					}
					double count = static_cast<double>(bottom - top) * (right - left);
					for (int c = 0; c < 3; ++c) out[(static_cast<std::size_t>(y) * w + x) * 3 + c] = sum[c] / count;
				}
			}
			return out;
		}

		// What a picture is drawn from: what its crop keeps, its colors moved as the
		// collage's matching says, and what names that, for what's worked out from it.
		struct look {
			desktop::picture picture;
			std::string key;
		};

		// A picture as what its crop keeps, its colors not yet moved: only that part, so
		// it sits, and what's around it is filled, by that part alone.
		std::string kept_key(const std::filesystem::path& file, const value& layer) {
			model::box crop = crop_of(layer);
			return std::format("{} {} {} {} {} {} {}", file.string(), layer["turns"].as_double(), layer["mirrored"].as_double(), crop.x, crop.y, crop.w, crop.h);
		}
		look kept_look(const std::filesystem::path& file, const value& layer) {
			model::box crop = crop_of(layer);
			std::string key = kept_key(file, layer);
			desktop::picture turned = turned_at(file, layer);
			if (!turned || (crop.x <= 0 && crop.y <= 0 && crop.w >= 1 && crop.h >= 1)) return {turned, key};
			static kept_by<desktop::picture> made(64);
			return {made.get(key,
			                 [&] {
				                 int pw = turned.width(), ph = turned.height();
				                 int x = std::clamp(static_cast<int>(std::lround(crop.x * pw)), 0, pw - 1), y = std::clamp(static_cast<int>(std::lround(crop.y * ph)), 0, ph - 1);
				                 int w = std::clamp(static_cast<int>(std::lround(crop.w * pw)), 1, pw - x), h = std::clamp(static_cast<int>(std::lround(crop.h * ph)), 1, ph - y);
				                 std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4);
				                 const auto& rgba = turned.rgba();
				                 for (int row = 0; row < h; ++row) {
					                 std::copy_n(rgba.begin() + ((static_cast<std::ptrdiff_t>(y + row) * pw + x) * 4), w * 4, pixels.begin() + static_cast<std::ptrdiff_t>(row) * w * 4);
				                 }
				                 return desktop::picture::of(w, h, std::move(pixels));
			                 }),
			        key};
		}

		// A small copy of what's drawn, no more than 512 on a side, made once: what halos,
		// blends and color measuring work from, so none of them reads every pixel of a
		// large photo again.
		desktop::picture small_of(const look& seen) {
			static kept_by<desktop::picture> made(64);
			return made.get(seen.key, [&] {
				int pw = seen.picture.width(), ph = seen.picture.height();
				double s = std::min(1.0, 512.0 / std::max(pw, ph));
				int w = std::max(1, static_cast<int>(std::lround(pw * s))), h = std::max(1, static_cast<int>(std::lround(ph * s)));
				auto colors = averaged(seen.picture, {0, 0, 1, 1}, w, h);
				std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4, 255);
				for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i) {
					for (std::size_t c = 0; c < 3; ++c) pixels[i * 4 + c] = static_cast<std::uint8_t>(std::clamp(std::lround(colors[i * 3 + c]), 0L, 255L));
				}
				return desktop::picture::of(w, h, std::move(pixels));
			});
		}

		// A picture's colors, measured once from a small copy of it.
		kept_by<model::color_stats> measured_colors(256);
		model::color_stats stats_kept(const look& seen, double weight) {
			model::color_stats out = measured_colors.get(seen.key, [&] { return model::stats_of(averaged(small_of(seen), {0, 0, 1, 1}, 128, 128), 1); });
			out.weight = weight;
			return out;
		}

		// A picture no more than `largest` on its longer side, each pixel the average of
		// those it covers, made once; as it is when it's that small already.
		look no_larger(const look& seen, int largest) {
			int pw = seen.picture.width(), ph = seen.picture.height();
			if (largest <= 0 || std::max(pw, ph) <= largest) return seen;
			std::string key = std::format("{} at most {}", seen.key, largest);
			static kept_by<desktop::picture> made(32);
			return {made.get(key,
			                 [&] {
				                 double s = static_cast<double>(largest) / std::max(pw, ph);
				                 int w = std::max(1, static_cast<int>(std::lround(pw * s))), h = std::max(1, static_cast<int>(std::lround(ph * s)));
				                 auto colors = averaged(seen.picture, {0, 0, 1, 1}, w, h);
				                 std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4, 255);
				                 for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i) {
					                 for (std::size_t c = 0; c < 3; ++c) pixels[i * 4 + c] = static_cast<std::uint8_t>(std::clamp(std::lround(colors[i * 3 + c]), 0L, 255L));
				                 }
				                 return desktop::picture::of(w, h, std::move(pixels));
			                 }),
			        key};
		}

		// A picture as it's drawn: what its crop keeps, no more than `largest` on its
		// longer side when that says, its colors moved as its shift says, worked out once
		// on the processor, its rows shared among its cores.
		look look_at(const std::filesystem::path& file, const value& layer, const model::color_shift* shift, int largest = 0) {
			look seen = kept_look(file, layer);
			if (!seen.picture || !shift) return seen;
			seen = no_larger(seen, largest);
			const auto& m = *shift;
			std::string key = std::format("{} {:.3f} {:.3f} {:.3f} {:.1f} {:.1f} {:.1f}", seen.key, m.gain[0], m.gain[1], m.gain[2], m.offset[0], m.offset[1], m.offset[2]);
			for (double t : m.to) key += std::format(" {:.1f}", t);
			static kept_by<desktop::picture> made(32);
			return {made.get(key,
			                 [&] {
				                 std::vector<std::uint8_t> pixels = seen.picture.rgba();
				                 model::color_shifter shifter(m);
				                 std::size_t count = pixels.size() / 4;
				                 std::size_t parts = std::clamp<std::size_t>(std::thread::hardware_concurrency(), 1, 16);
				                 std::size_t each = (count + parts - 1) / parts;
				                 std::vector<std::thread> sharing;
				                 for (std::size_t start = 0; start < count; start += each) {
					                 sharing.emplace_back([&, start] { shifter.shift(pixels.data() + start * 4, std::min(each, count - start)); });
				                 }
				                 for (auto& t : sharing) t.join();
				                 return desktop::picture::of(seen.picture.width(), seen.picture.height(), std::move(pixels));
			                 }),
			        key};
		}

		// Whether a layer is harmonized: blend, as kept before the two were one, is too.
		bool harmonizes(const value& layer) {
			std::string around = layer["around"].as_string();
			return around == "harmonize" || around == "blend";
		}

		// With around harmonize, a picture's halo: worked out small, its own
		// colors spread past its edges and fading into what's under it (model::halo), and
		// drawn over it, as much bigger as `grow_x` and `grow_y` say, each a share of its
		// width and height.
		struct halo_drawn {
			desktop::picture picture;
			double grow_x = 0, grow_y = 0;
		};

		std::optional<halo_drawn> halo_of(const value& layer, const look& seen) {
			const double feather = model::halo_reach;
			model::box b = box_of(layer);
			// So its edge melts into what's filled from it, as seamless.
			if (!harmonizes(layer) || b.w <= 0 || b.h <= 0 || !seen.picture) return std::nullopt;
			int sw = b.w >= b.h ? 64 : std::max(1, static_cast<int>(std::lround(64 * b.w / b.h)));
			int sh = b.w >= b.h ? std::max(1, static_cast<int>(std::lround(64 * b.h / b.w))) : 64;
			int reach = std::max(4, static_cast<int>(std::lround(feather * std::min(sw, sh))));
			static kept_by<halo_drawn> made(64);
			return made.get(std::format("{} {} {} {}", seen.key, feather, sw, sh), [&] {
				auto spread = model::halo(sw, sh, averaged(small_of(seen), {0, 0, 1, 1}, sw, sh), reach, reach * model::halo_inside);
				std::vector<std::uint8_t> bytes(spread.size());
				for (std::size_t i = 0; i < spread.size(); ++i) bytes[i] = static_cast<std::uint8_t>(std::clamp(std::lround(spread[i]), 0L, 255L));
				return halo_drawn{desktop::picture::of(sw + 2 * reach, sh + 2 * reach, std::move(bytes)), static_cast<double>(reach) / sw, static_cast<double>(reach) / sh};
			});
		}

		// A halo drawn over the picture it's of, where that's drawn.
		void paint_halo(desktop::canvas& c, const halo_drawn& h, desktop::rect on) {
			float gx = static_cast<float>(h.grow_x) * on.width, gy = static_cast<float>(h.grow_y) * on.height;
			c.picture(h.picture, {on.x - gx, on.y - gy, on.width + 2 * gx, on.height + 2 * gy}, {0, 0, 1, 1});
		}

		// A picture's edge color: the average of its outermost pixels, once.
		desktop::color edge_color(const std::string& kept, const desktop::picture& p) {
			static kept_by<desktop::color> made(256);
			return made.get(kept, [&] {
				const auto& rgba = p.rgba();
				int w = p.width(), h = p.height();
				double sum[3] = {};
				long long count = 0;
				auto add = [&](int x, int y) {
					for (int c = 0; c < 3; ++c) sum[c] += rgba[(static_cast<std::size_t>(y) * w + x) * 4 + c];
					++count;
				};
				for (int x = 0; x < w; ++x) add(x, 0), add(x, h - 1);
				for (int y = 1; y < h - 1; ++y) add(0, y), add(w - 1, y);
				auto channel = [&](int c) { return static_cast<std::uint32_t>(std::lround(sum[c] / static_cast<double>(std::max(1LL, count)))); };
				return desktop::color::hex(channel(0) << 16 | channel(1) << 8 | channel(2));
			});
		}

		// The rest of the collage around a picture, `board` in the window, filled from it:
		// its outermost pixels drawn out to the collage's edges, or with around color, its
		// edge color.
		void paint_around(desktop::canvas& c, desktop::rect board, desktop::rect shown, const std::string& kept, const desktop::picture& p, const std::string& around) {
			if (shown.x <= board.x && shown.y <= board.y && shown.right() >= board.right() && shown.bottom() >= board.bottom()) return;
			c.box(board, {.fill = edge_color(kept, p)});
			if (around == "color") return;
			float u = 0.5f / static_cast<float>(p.width()), v = 0.5f / static_cast<float>(p.height());
			float left = shown.x - board.x, right = board.right() - shown.right(), top = shown.y - board.y, bottom = board.bottom() - shown.bottom();
			auto strip = [&](desktop::rect area, desktop::rect from) {
				if (area.width > 0 && area.height > 0) c.picture(p, area, from);
			};
			strip({board.x, shown.y, left + 1, shown.height}, {0, 0, u, 1});
			strip({shown.right() - 1, shown.y, right + 1, shown.height}, {1 - u, 0, u, 1});
			strip({shown.x, board.y, shown.width, top + 1}, {0, 0, 1, v});
			strip({shown.x, shown.bottom() - 1, shown.width, bottom + 1}, {0, 1 - v, 1, v});
			strip({board.x, board.y, left + 1, top + 1}, {0, 0, u, v});
			strip({shown.right() - 1, board.y, right + 1, top + 1}, {1 - u, 0, u, v});
			strip({board.x, shown.bottom() - 1, left + 1, bottom + 1}, {0, 1 - v, u, v});
			strip({shown.right() - 1, shown.bottom() - 1, right + 1, bottom + 1}, {1 - u, 1 - v, u, v});
		}

		// Words as a layer has them, at `size` in the window, over a soft dark shadow so
		// they read on most photos, white unless they say.
		// Words are in the theme's text font, as the web draws them, which an app can bring;
		// one drawn away from the window's thread is told it.
		desktop::text_style words_style(double size, int weight, desktop::color color, const std::string& family = current_theme().text_family, bool italic = false) {
			return {.family = family, .size = static_cast<float>(size), .weight = weight, .italic = italic, .color = color, .line_height = 1.2f};
		}
		int weight_of(const value& layer) { return layer["weight"].is_null() ? 700 : static_cast<int>(layer["weight"].as_double()); }
		bool italic_of(const value& layer) { return layer["italic"].as_double() != 0; }

		// How big words are on the collage, in its own pixels.
		std::pair<double, double> measured(const std::string& words, double size, int weight, bool italic) {
			auto laid = desktop::lay_out(desktop::text_span{words.empty() ? " " : words, words_style(size, weight, {}, current_theme().text_family, italic)}, unwrapped, 1.0);
			return {std::max(1.f, laid.width()), std::max(1.f, laid.height())};
		}

		// Words drawn at `at` in the window, and where the caret stands among them.
		desktop::rect paint_words(desktop::canvas& c, const value& layer, const std::string& said, double size, desktop::point at, std::size_t caret = std::string::npos,
		                          const std::string& family = current_theme().text_family) {
			int weight = weight_of(layer);
			std::string words = said.empty() ? " " : said;
			desktop::color shade = desktop::color::hex(0x000000);
			shade.alpha = 0.45f;
			float off = std::max(1.f, static_cast<float>(size) * 0.04f);
			bool italic = italic_of(layer);
			c.text(desktop::lay_out(desktop::text_span{words, words_style(size, weight, shade, family, italic)}, unwrapped, c.scale()), {at.x + off / 2, at.y + off});
			auto laid = desktop::lay_out(desktop::text_span{words, words_style(size, weight, color_of(layer["color"].as_string(), desktop::color::hex(0xffffff)), family, italic)}, unwrapped, c.scale());
			c.text(laid, at);
			auto end = laid.caret(std::min(caret, said.size()));
			float line = static_cast<float>(size) * 1.2f;
			return {at.x + end.x / static_cast<float>(c.scale()), at.y + static_cast<float>(end.line) * line, 2, line};
		}

		// What the pointer is doing to a layer, from where it was and where the pointer was.
		struct gesture {
			enum { none, moving, resizing, cropping, shifting, drawing, reshaping, selecting, panning } what = none;
			std::string id;
			model::box from;
			desktop::point at;
			int corner = 0;
			int edges = 0;                           // a crop's edges dragged, or none to move it; a path's control point
			model::box kept_from = {};               // while a crop's picture shifts, what's kept as it was
			bool free = false;                       // a shape, resized freely, its shape kept only with Shift
		};

		// A path as a reshape has it so far: its node moved, or one of its control points,
		// in edges 1 coming in and 2 going out.
		std::string reshaped_by(const std::string& outline, const gesture& g, double dx, double dy) {
			auto node = static_cast<std::size_t>(g.corner);
			return g.edges ? model::path_handle_moved(outline, node, g.edges == 2, dx, dy) : model::path_moved(outline, node, dx, dy);
		}

		// Where the collage was last drawn in the window, and how much smaller than
		// itself; what the pointer is doing; where a layer is going, or was let go until
		// the view has it there; the lines it snaps to and snapped to; a crop being made,
		// or words being written; and the buttons over the picked layer, as last drawn.
		struct placed {
			desktop::rect board{};
			float scale = 0;
			gesture now;
			std::optional<std::pair<std::string, model::box>> going;
			model::lines lines;
			std::optional<double> guide_x, guide_y;
			std::optional<std::pair<model::point, model::point>> corner;
			struct crop_edit {
				std::string id;
				model::box whole, kept;
			};
			std::optional<crop_edit> crop;
			struct writing {
				std::string id, words;
				bool fresh = false;                     // all chosen, so the first key typed replaces them
				std::size_t caret = std::string::npos;  // the byte it stands before
				// Words typed where a draw by typing was pressed, made only once they're
				// kept: what they'll be made of, by that draw, its parts by name.
				std::optional<value> unmade = std::nullopt;
				std::optional<collage_draw> by = std::nullopt;
			};
			std::optional<writing> editing;
			std::optional<model::box> drawing;  // a layer being drawn, as big as the drag so far
			std::optional<model::line> line;    // or a line, as far as the drag has gone
			std::vector<model::point> hand;     // or what a drag by hand has gone through
			std::vector<model::pen_node> pen;   // or the nodes a pen has put down so far
			std::optional<model::point> hover;  // and where the pointer is, which the next segment follows
			// A path being reshaped: its outline as it was, and as it is now, by its id.
			std::string reshape_from;
			std::optional<std::pair<std::string, std::string>> reshaped;
			// The picked path's node last pressed, whose control points show, by its id.
			std::optional<std::pair<std::string, std::size_t>> node;
			// How it's looked at, zoomed and moved, and the area it was last drawn in.
			model::view_at view;
			desktop::rect area{};
			float margin = 32;
			// The group each layer is in, as last drawn; and while a group is moved or
			// resized, where it and each it holds were as it began.
			std::map<std::string, std::string> group_of;
			model::box carried_group;
			std::map<std::string, model::box> carried_from;
			std::string carrying;  // the group carried, or "chosen", what's chosen
			// A group gone into by pressing it twice: what's pressed in it is picked from
			// among what it holds, until Esc or a press outside it.
			std::string entered;
			// Several picked at once, by a drag around them or with Shift, besides the
			// screen's pick of one; and a drag around what's to be, as far as it's gone.
			std::vector<std::string> chosen;
			std::optional<model::box> marquee;
			// What's picked by a press on `id`: the group it's in just inside `top`, or
			// the outermost when `top` is none, or itself.
			std::string below(std::string id, const std::string& top) const {
				for (int depth = 0; depth < 64; ++depth) {
					auto up = group_of.find(id);
					if (up == group_of.end() || up->second == top) break;
					id = up->second;
				}
				return id;
			}
			// Whether a layer is in a group, however deep.
			bool inside(std::string id, const std::string& group) const {
				for (int depth = 0; depth < 64; ++depth) {
					auto up = group_of.find(id);
					if (up == group_of.end()) return false;
					if (up->second == group) return true;
					id = up->second;
				}
				return false;
			}
			// The outermost group it's in, or itself.
			std::string outermost(std::string id) const {
				for (int depth = 0; depth < 64; ++depth) {
					auto up = group_of.find(id);
					if (up == group_of.end()) break;
					id = up->second;
				}
				return id;
			}
			std::vector<std::pair<desktop::rect, std::size_t>> tools;
			model::box on_board(const model::box& b) const {
				return {board.x + b.x * scale, board.y + b.y * scale, b.w * scale, b.h * scale};
			}
			model::point on_collage(desktop::point at) const { return {(at.x - board.x) / scale, (at.y - board.y) / scale}; }
		};

		// Where each layer is on the collage: where it says, or, being moved or let go,
		// where it's going.
		model::box where_is(const value& layer, placed& w) {
			model::box b = box_of(layer);
			std::string id = layer["id"].as_string();
			if (auto& g = w.going; g && g->first == id) {
				auto near = [](double a, double z) { return std::abs(a - z) < 0.01; };
				bool there = near(b.x, g->second.x) && near(b.y, g->second.y) && near(b.w, g->second.w) && near(b.h, g->second.h);
				if (there && w.now.what == gesture::none) g.reset(), w.carried_from.clear();
				else b = g->second;
			} else if (auto from = w.carried_from.find(id); g && from != w.carried_from.end() && g->first == w.carrying) {
				// One a group holds, carried with it.
				b = model::carried(from->second, w.carried_group, g->second);
			}
			return b;
		}

		// A button over the picked layer run: the collage's delete of a group deletes all
		// it holds with it, in one step.
		void run_over(page& p, const collage_spec& spec, const placed& w, const value& layers, const std::string& name, const value& sent) {
			std::string id = sent["id"].as_string();
			bool group = std::ranges::any_of(layers.as_array(), [&](const value& l) { return l["id"].as_string() == id && holds_others(l); });
			if (name != spec.remove || !group) return p.run(name, sent);
			bool joined = false;
			for (const auto& l : layers.as_array()) {
				if (!w.inside(l["id"].as_string(), id)) continue;
				value one = value(fields{{"id", l["id"]}});
				if (std::exchange(joined, true)) one.fields["one:history"] = value("joined");
				p.run(name, one);
			}
			value gone = sent;
			gone.fields["one:history"] = value("joined");
			p.run(name, gone);
		}

		// A group taken to `to`: each it holds carried with it, all one step to undo.
		void place_carried(page& p, placed& w, const collage_spec& spec, model::box to, std::function<void(const answer&)> back = {}) {
			bool joined = false;
			// Once the last is answered, it's drawn from the view again.
			std::size_t left = w.carried_from.size();
			auto answered = std::make_shared<std::size_t>(0);
			for (const auto& [id, from] : w.carried_from) {
				model::box b = model::carried(from, w.carried_group, to);
				value sent = collage_fields_of(value(fields{{"id", id}, {"x", b.x}, {"y", b.y}, {"w", b.w}, {"h", b.h}}), spec.fields);
				if (std::exchange(joined, true)) sent.fields["one:history"] = value("joined");
				p.run(spec.place, sent, [&w, back, answered, left](const answer& a) {
					if (back) back(a);
					if (++*answered == left && w.carrying == "chosen") w.going.reset(), w.carried_from.clear();
				});
			}
		}

		// A group's gesture begun, or what's chosen: where it and each it holds are.
		void carry_from(placed& w, const value& layers, const std::string& group, model::box from) {
			w.carried_from.clear();
			w.carried_group = from;
			w.carrying = group;
			for (const auto& l : layers.as_array()) {
				std::string id = l["id"].as_string();
				if (holds_others(l)) continue;
				bool carried = group == "chosen" ? std::ranges::any_of(w.chosen, [&](const std::string& c) { return c == id || w.inside(id, c); }) : w.inside(id, group);
				if (carried) w.carried_from[id] = box_of(l);
			}
		}
		// The box around what's chosen, where each is.
		model::box chosen_box(placed& w, const value& layers) {
			std::vector<model::box> boxes;
			for (const auto& l : layers.as_array()) {
				if (std::ranges::contains(w.chosen, l["id"].as_string())) boxes.push_back(where_is(l, w));
			}
			return model::around(boxes);
		}
		// What's chosen made the screen's pick: one alone is picked; several, none is,
		// and they're drawn chosen.
		void pick_chosen(page& p, const collage_spec& spec, placed& w) {
			if (w.chosen.size() == 1) {
				if (!spec.pick.empty()) p.pick(spec.pick, w.chosen.front());
				w.chosen.clear();
				return;
			}
			if (!spec.pick.empty()) p.pick(spec.pick, "");
		}

		// A crop begun on a picture where it is, and the whole picture behind it.
		placed::crop_edit crop_begun(const value& layer) {
			model::box b = box_of(layer);
			return {layer["id"].as_string(), model::whole_of(b, crop_of(layer)), b};
		}

		// The lines a layer snaps to: the collage's edges and middle, and every other layer's.
		void snap_lines(const value& layers, double width, double height, placed& w, const std::string& moving) {
			std::vector<model::box> others;
			for (const auto& l : layers.as_array()) {
				std::string id = l["id"].as_string();
				if (id != moving && !w.inside(id, moving) && !w.carried_from.contains(id) && !holds_others(l)) others.push_back(where_is(l, w));
			}
			w.lines = model::snap_lines(width, height, others);
		}

		// Where a gesture takes its layer, the pointer now at `at` in the window.
		model::box going_to(placed& w, desktop::point at, double width, double height, bool snapping) {
			const gesture& g = w.now;
			double dx = (at.x - g.at.x) / w.scale, dy = (at.y - g.at.y) / w.scale, close = 8 / w.scale;
			bool snaps = snapping && !desktop::held().alt;
			if (g.what == gesture::cropping && w.crop) return model::cropped(g.from, g.edges, dx, dy, w.crop->whole);
			model::guided to;
			if (g.what == gesture::moving) to = model::moved(g.from, dx, dy, w.lines, width, height, close, snaps);
			else to = model::resized(g.from, g.corner, dx, dy, g.free ? desktop::held().shift : !desktop::held().shift, w.lines, close, snaps);
			w.guide_x = to.guide_x, w.guide_y = to.guide_y, w.corner = to.corner;
			return to.at;
		}

		// A crop's picture and what's kept, moved together as far as the pointer has.
		void shift_crop(placed& w, desktop::point at) {
			if (!w.crop || w.scale <= 0) return;
			const gesture& g = w.now;
			double dx = (at.x - g.at.x) / w.scale, dy = (at.y - g.at.y) / w.scale;
			w.crop->whole = {g.from.x + dx, g.from.y + dy, g.from.w, g.from.h};
			w.crop->kept = {g.kept_from.x + dx, g.kept_from.y + dy, g.kept_from.w, g.kept_from.h};
		}

		// A crop kept: the part of the picture kept, as fractions of it, and where it is.
		void keep_crop(page& p, placed& w, const collage_spec& spec) {
			if (!w.crop) return;
			const auto& e = *w.crop;
			model::box crop{(e.kept.x - e.whole.x) / e.whole.w, (e.kept.y - e.whole.y) / e.whole.h, e.kept.w / e.whole.w, e.kept.h / e.whole.h};
			w.going = std::pair{e.id, e.kept};
			p.run(spec.crop, collage_fields_of(value(fields{{"id", e.id}, {"crop_x", crop.x}, {"crop_y", crop.y}, {"crop_w", crop.w}, {"crop_h", crop.h},
			                                                {"x", e.kept.x}, {"y", e.kept.y}, {"w", e.kept.w}, {"h", e.kept.h}}),
			                                   spec.fields));
			w.crop.reset();
		}

		void make_drawn(page& p, const collage_spec& spec, const value& layers, const collage_draw& d, value made);

		// Words written kept, as big as they now are; written away to nothing, gone.
		void finish_writing(page& p, placed& w, const value& layers, const collage_spec& spec) {
			if (!w.editing) return;
			auto e = *std::exchange(w.editing, std::nullopt);
			if (e.unmade) {
				// Typed: made now, as big as they are; nothing typed, nothing made.
				if (e.words.find_first_not_of(" \n\t") == std::string::npos) return;
				value made = *e.unmade;
				auto [mw, mh] = measured(e.words, made["size"].as_double(), weight_of(made), italic_of(made));
				made.fields["words"] = value(e.words), made.fields["w"] = value(mw), made.fields["h"] = value(mh);
				make_drawn(p, spec, layers, *e.by, made);
				return;
			}
			for (const auto& l : layers.as_array()) {
				if (l["id"].as_string() != e.id) continue;
				if (e.words.find_first_not_of(" \n\t") == std::string::npos) {
					if (!spec.remove.empty()) p.run(spec.remove, value(fields{{"id", e.id}}));
					if (!spec.pick.empty()) p.pick(spec.pick, "");
					return;
				}
				if (e.words == l["words"].as_string()) return;
				auto [mw, mh] = measured(e.words, l["size"].as_double(), weight_of(l), italic_of(l));
				p.run(spec.write, collage_fields_of(value(fields{{"id", e.id}, {"words", e.words}, {"size", l["size"]}, {"x", l["x"]}, {"y", l["y"]}, {"w", mw}, {"h", mh}}), spec.fields));
			}
		}

		// A key while words are written: Esc keeps them, Enter starts a new line, the
		// arrows, Home and End move the caret, and the rest is typed.
		bool written(placed::writing& e, const key_event& k) {
			std::string& words = e.words;
			std::size_t& at = e.caret;
			at = std::min(at, words.size());
			auto before = [&](std::size_t i) {
				do --i;
				while (i > 0 && (static_cast<unsigned char>(words[i]) & 0xc0) == 0x80);
				return i;
			};
			auto after = [&](std::size_t i) {
				do ++i;
				while (i < words.size() && (static_cast<unsigned char>(words[i]) & 0xc0) == 0x80);
				return i;
			};
			bool typed = !k.text.empty() && !k.control && !k.alt && !k.super && k.text[0] != '\r';
			bool new_line = k.is(keys::enter) || k.is(keys::keypad_enter);
			if (e.fresh && (typed || new_line || k.is(keys::backspace) || k.is(keys::delete_key))) words.clear(), at = 0, e.fresh = false;
			e.fresh = e.fresh && !(k.is(keys::left) || k.is(keys::right) || k.is(keys::home) || k.is(keys::end));
			if (new_line) words.insert(at++, 1, '\n');
			else if (k.is(keys::backspace) && at > 0) {
				std::size_t from = before(at);
				words.erase(from, at - from), at = from;
			} else if (k.is(keys::delete_key) && at < words.size()) words.erase(at, after(at) - at);
			else if (k.is(keys::left) && at > 0) at = before(at);
			else if (k.is(keys::right) && at < words.size()) at = after(at);
			else if (k.is(keys::home)) {
				std::size_t line_break = at == 0 ? std::string::npos : words.rfind('\n', at - 1);
				at = line_break == std::string::npos ? 0 : line_break + 1;
			} else if (k.is(keys::end)) at = std::min(words.find('\n', at), words.size());
			else if (typed) words.insert(at, k.text), at += k.text.size();
			else return false;
			return true;
		}


		// The layer new objects go in, worked out as the collage is built: the one picked,
		// or the topmost; none while it isn't held in layers, or has none yet.
		std::string layer_now(page& p, const collage_spec& spec) { return p.kept<std::string>("collage layer " + spec.key).peek(); }

		// What was last copied or cut from a collage, to be pasted back as itself: its
		// layers, a group's all it holds, and the SVG put on the clipboard with them.
		struct copied_layers {
			std::string svg;
			std::vector<value> rows;
		};
		copied_layers& copied() {
			static copied_layers kept;
			return kept;
		}


		// Whether there's a layer for a new object to go in. When it's held in layers and
		// has none yet, one's made first, Layer 1, and `again` runs once it's there.
		bool layer_ready(page& p, const collage_spec& spec, std::function<void()> again) {
			if (spec.layers.empty() || spec.layer_create.empty() || !layer_now(p, spec).empty()) return true;
			value made = value(fields{{"name", "Layer 1"}, {"depth", 0}});
			if (!spec.layer_parent.empty()) made.fields[spec.layer_parent] = value(spec.subject);
			p.run(spec.layer_create, made, [&p, kept = p.kept<std::string>("collage layer " + spec.key), pick = spec.layer_pick, again](const answer& a) {
				if (!a) return;
				kept.set(*a);
				if (!pick.empty()) p.pick(pick, *a);
				again();
			});
			return false;
		}

		// What was copied, made again where it was, in the current layer, above what's
		// there: each group before what it holds, in it again; all one step to undo; then
		// it's picked.
		void paste_objects(page& p, const collage_spec& spec, const value& layers) {
			if (spec.add.empty()) return;
			auto rows = copied().rows;
			std::set<std::string> ids;
			for (const auto& r : rows) ids.insert(r["id"].as_string());
			// Groups first, outermost first, then the rest, as they were stacked.
			auto nesting = [&](const value& r) {
				int depth = 0;
				for (std::string at = r["group"].as_string(); ids.contains(at) && depth < 64; ++depth) {
					auto up = std::ranges::find_if(rows, [&](const value& o) { return o["id"].as_string() == at; });
					at = up == rows.end() ? "" : (*up)["group"].as_string();
				}
				return depth;
			};
			std::ranges::stable_sort(rows, [&](const value& a, const value& b) {
				bool ga = holds_others(a), gb = holds_others(b);
				if (ga != gb) return ga;
				return ga && nesting(a) < nesting(b);
			});
			double top = 0;
			for (const auto& l : layers.as_array()) top = std::max(top, l["depth"].as_double());
			auto made = std::make_shared<std::map<std::string, std::string>>();
			auto next = std::make_shared<std::function<void(std::size_t)>>();
			std::weak_ptr<std::function<void(std::size_t)>> going_on = next;
			*next = [&p, spec, rows, ids, made, going_on, top](std::size_t i) {
				std::string layer = layer_now(p, spec);
				if (i == rows.size()) {
					// The ones not in another pasted with them, picked.
					std::vector<std::string> picked;
					for (const auto& r : rows) {
						if (!ids.contains(r["group"].as_string())) picked.push_back((*made)[r["id"].as_string()]);
					}
					if (picked.size() == 1 && !spec.pick.empty()) p.pick(spec.pick, picked.front());
					return;
				}
				const value& r = rows[i];
				value again = value::map();
				for (const auto& [part, field] : spec.fields) {
					if (part == "group" || part == "hidden" || !r.fields.contains(part)) continue;
					again.fields[part] = r[part];
				}
				again.fields["depth"] = value(top + 1 + static_cast<double>(i));
				std::string group = r["group"].as_string();
				if (spec.fields.contains("group")) again.fields["group"] = ids.contains(group) ? value((*made)[group]) : value();
				if (!spec.parent.empty()) again.fields[spec.parent] = value(spec.subject);
				if (!spec.layer.empty() && !layer.empty()) again.fields[spec.layer] = value(layer);
				value sent = collage_fields_of(again, spec.fields);
				if (i > 0) sent.fields["one:history"] = value("joined");
				p.run(spec.add, sent, [made, next = going_on.lock(), i, id = r["id"].as_string()](const answer& a) {
					if (!a || !next) return;
					(*made)[id] = *a;
					(*next)(i + 1);
				});
			};
			if (!layer_ready(p, spec, [next] { (*next)(0); })) return;
			(*next)(0);
		}

		// A new object put in the layer it goes in.
		void in_layer(page& p, const collage_spec& spec, value& made) {
			if (!spec.layer.empty()) made.fields[spec.layer] = value(layer_now(p, spec));
		}

		// Whether a collage keeps shapes, so an SVG brought in can be its own.
		bool keeps_shapes(const collage_spec& spec) {
			return std::ranges::all_of(std::array{"shape", "points", "fill", "stroke", "stroke_width"}, [&](const char* part) { return spec.fields.contains(part); });
		}

		// An SVG brought in as its own shapes and curves, each an object, where it would
		// sit as a picture: as big as it is, smaller where it wouldn't fit, centered on
		// `on` or the middle. Held in layers, they're a new layer, named for it, on top;
		// all one step to undo, and the layer picked.
		void add_shapes(page& p, const collage_spec& spec, const value& layers, double width, double height, const std::string& svg, const std::string& name,
		                std::optional<model::point> on) {
			auto size = desktop::picture::svg_size(svg);
			auto shapes = desktop::svg_shapes(svg);
			if (!size || shapes.empty()) {
				p.told().set(name + " has no shapes to bring in.");
				return;
			}
			double s = std::min({1.0, width / size->first, height / size->second});
			double cx = on ? on->x : width / 2, cy = on ? on->y : height / 2;
			model::box at{cx - size->first * s / 2, cy - size->second * s / 2, size->first * s, size->second * s};
			const auto& all = layers.as_array();
			double depth = !all.empty() && !all.back()["depth"].is_null() ? all.back()["depth"].as_double() + 1 : 0;
			auto made = [&p, spec, shapes, at, s, depth](const std::string& layer, bool joined, const std::string& group = "") {
				double d = depth;
				for (const auto& shape : shapes) {
					model::box b = model::path_bounds(shape.path);
					std::string points = model::path_fractions(shape.path, b);
					model::box placed{at.x + b.x * s, at.y + b.y * s, b.w * s, b.h * s};
					if (placed.w < 1) placed.x += placed.w / 2 - 0.5, placed.w = 1;
					if (placed.h < 1) placed.y += placed.h / 2 - 0.5, placed.h = 1;
					value object = value(fields{{"shape", "path"}, {"points", points}, {"fill", shape.fill}, {"stroke", shape.stroke}, {"stroke_width", shape.stroke_width * s},
					                            {"x", placed.x}, {"y", placed.y}, {"w", placed.w}, {"h", placed.h}, {"depth", d++}});
					if (spec.fields.contains("opacity")) object.fields["opacity"] = value(shape.opacity);
					if (!spec.parent.empty()) object.fields[spec.parent] = value(spec.subject);
					if (!spec.layer.empty() && !layer.empty()) object.fields[spec.layer] = value(layer);
					if (!group.empty()) object.fields["group"] = value(group);
					value sent = collage_fields_of(object, spec.fields);
					if (std::exchange(joined, true)) sent.fields["one:history"] = value("joined");
					p.run(spec.add, sent);
				}
			};
			// Where layers are grouped, one group in the layer it's brought into, named for it,
			// as big as it is; its shapes made once it is, all one step to undo.
			if (spec.fields.contains("group")) {
				auto bring = [&p, spec, made, at, name, depth] {
					std::string layer = layer_now(p, spec);
					value group = value(fields{{"x", at.x}, {"y", at.y}, {"w", at.w}, {"h", at.h}, {"depth", depth}});
					if (spec.fields.contains("name")) group.fields["name"] = value(name);
					if (!spec.parent.empty()) group.fields[spec.parent] = value(spec.subject);
					if (!spec.layer.empty() && !layer.empty()) group.fields[spec.layer] = value(layer);
					p.run(spec.add, collage_fields_of(group, spec.fields), [&p, spec, made, layer](const answer& a) {
						if (!a) return;
						made(layer, true, *a);
						if (!spec.pick.empty()) p.pick(spec.pick, *a);
					});
				};
				if (!layer_ready(p, spec, bring)) return;
				return bring();
			}
			if (spec.layers.empty() || spec.layer_create.empty()) return made(layer_now(p, spec), false);
			// A new layer, named for it, on top of the rest.
			value layer = value(fields{{"name", name}, {"depth", p.kept<double>("collage top layer " + spec.key).peek() + 1}});
			if (!spec.layer_parent.empty()) layer.fields[spec.layer_parent] = value(spec.subject);
			p.run(spec.layer_create, layer, [&p, made, pick = spec.layer_pick](const answer& a) {
				if (!a) return;
				if (!pick.empty()) p.pick(pick, *a);
				made(*a, true);
			});
		}

		// Pictures dropped or pasted, to come in where they were let go, or in the middle.
		void take_pictures(page& p, const collage_spec& spec, std::vector<std::filesystem::path> files, std::optional<model::point> on) {
			if (spec.add.empty() || !spec.paste || !spec.fields.contains("picture")) return;
			p.kept<std::optional<model::point>>("collage pictures at " + spec.key).set(on);
			pictures_coming(p, spec.key, std::move(files));
		}

		// Those come in: the first on a collage with nothing on it covers it, and the rest,
		// and every SVG, are as big as they are, smaller where they wouldn't fit, centered
		// where they were let go, each a little further along; all one step to undo. The
		// last of them is picked once it's made, so it's ready to be worked on.
		void add_pictures(page& p, const collage_spec& spec, const value& layers, double width, double height, const std::vector<coming_picture>& pictures,
		                  std::optional<model::point> on) {
			// The layer pictures go in, made first when there's none; an SVG makes its own.
			bool any = std::ranges::any_of(pictures, [&](const auto& c) { return c.svg.empty() || !keeps_shapes(spec); });
			if (any && !layer_ready(p, spec, [&p, spec, layers, width, height, pictures, on] { add_pictures(p, spec, layers, width, height, pictures, on); })) return;
			const auto& all = layers.as_array();
			double depth = !all.empty() && !all.back()["depth"].is_null() ? all.back()["depth"].as_double() + 1 : 0;
			bool covered = !all.empty();
			for (const auto& picture : pictures) {
				double at = static_cast<double>(picture.index);
				// An SVG, where the collage keeps shapes, comes in as its own shapes.
				if (!picture.svg.empty() && keeps_shapes(spec)) {
					std::string name = picture.file.stem().string().starts_with("one-picture-") ? "Pasted" : picture.file.stem().string();
					std::optional<model::point> where = on;
					if (where && at) where = model::point{where->x + 40.0 * at, where->y + 40.0 * at};
					add_shapes(p, spec, layers, width, height, picture.svg, name, where);
					continue;
				}
				double pw = picture.width, ph = picture.height;
				model::box b;
				// An SVG is drawing, not a photograph: it comes in at its own size, fitted,
				// never covering the collage.
				if (!covered && picture.svg.empty()) {
					b = model::sitting("fill", pw, ph, width, height);
					covered = true;
				} else {
					double s = std::min({1.0, width / pw, height / ph});
					double cx = on ? on->x : width / 2, cy = on ? on->y : height / 2;
					b = {cx - pw * s / 2 + 40.0 * at, cy - ph * s / 2 + 40.0 * at, pw * s, ph * s};
				}
				value made = value(fields{{"x", b.x}, {"y", b.y}, {"w", b.w}, {"h", b.h}});
				if (!spec.parent.empty()) made.fields[spec.parent] = value(spec.subject);
				in_layer(p, spec, made);
				if (!all.empty() && !all.back()["depth"].is_null()) made.fields["depth"] = value(depth++);
				if (spec.fields.contains("picture_width")) made.fields["picture_width"] = value(pw), made.fields["picture_height"] = value(ph);
				if (picture.joined) made.fields["one:history"] = value("joined");
				run_with_picture(p, spec.add, collage_fields_of(made, spec.fields), spec.fields.at("picture"), picture, [&p, pick = spec.pick](const answer& a) {
					if (a && !pick.empty()) p.pick(pick, *a);
				});
			}
		}

		// What a drag on it draws now: the first of its draws whose when holds, if any.
		const collage_draw* drawing_now(const collage_spec& spec) {
			for (const auto& d : spec.draws) {
				if (d.when) return &d;
			}
			return nullptr;
		}

		// A layer drawn, on top of the rest, picked once it's made; then the choices its
		// when read go back to their first, as a tool goes back to Select. The same as
		// the web's.
		// What a draw gives what it makes, and what its buttons last gave, where it takes
		// it, by the fields they're kept in.
		value given_by(page& p, const collage_draw& d) {
			value given = d.fields;
			for (const auto& taking : d.with) {
				value last = last_given(p, taking.from);
				for (const auto& name : taking.fields) {
					if (auto found = last.fields.find(name); found != last.fields.end()) given.fields[name] = found->second;
				}
			}
			return given;
		}

		// A layer made by a draw, of these parts: in the current layer, on top of the rest,
		// picked once it's made; then the choices its when read go back to their first, as
		// a tool goes back to Select. The same as the web's.
		void make_drawn(page& p, const collage_spec& spec, const value& layers, const collage_draw& d, value made) {
			if (!layer_ready(p, spec, [&p, spec, layers, d, made] { make_drawn(p, spec, layers, d, made); })) return;
			in_layer(p, spec, made);
			if (!spec.parent.empty()) made.fields[spec.parent] = value(spec.subject);
			const auto& all = layers.as_array();
			if (!all.empty() && !all.back()["depth"].is_null()) made.fields["depth"] = value(all.back()["depth"].as_double() + 1);
			value sent = collage_fields_of(made, spec.fields);
			for (const auto& [name, given] : given_by(p, d).fields) sent.fields[name] = given;
			p.run(d.command, sent, [&p, pick = spec.pick](const answer& a) {
				if (a && !pick.empty()) p.pick(pick, *a);
			});
			for (const auto& [key, first] : d.back) p.choose(key, first);
		}

		// A layer drawn, as big as the drag, or a line's points.
		void draw_layer(page& p, const collage_spec& spec, const value& layers, model::box b, std::optional<std::string> points = std::nullopt) {
			const collage_draw* d = drawing_now(spec);
			if (!d) return;
			value made = value(fields{{"x", b.x}, {"y", b.y}, {"w", b.w}, {"h", b.h}});
			if (points) made.fields["points"] = value(*points);
			make_drawn(p, spec, layers, *d, made);
		}

		// Words begun where a draw by typing was pressed, a line of them centered there: as
		// large as it gives, or a twelfth of the collage's height, in what it gives and its
		// buttons last gave, by their parts' names, so they're drawn as they'll be made.
		placed::writing typing_at(page& p, const collage_spec& spec, const collage_draw& d, model::point at, double height) {
			value given = given_by(p, d), made = value::map();
			for (const auto& [part, field] : spec.fields) {
				if (auto found = given.fields.find(field); found != given.fields.end()) made.fields[part] = found->second;
			}
			if (made["size"].as_double() <= 0) made.fields["size"] = value(std::max(8.0, std::round(height / 12)));
			auto [mw, mh] = measured("", made["size"].as_double(), weight_of(made), italic_of(made));
			made.fields["x"] = value(at.x), made.fields["y"] = value(at.y - mh / 2);
			return placed::writing{.id = "", .words = "", .fresh = false, .caret = 0, .unmade = made, .by = d};
		}

		// The path a pen drew, ended where it is or closed: made a layer as a line drawn is,
		// and the pen put away; fewer than two nodes draw nothing.
		void pen_done(page& p, const collage_spec& spec, const value& layers, placed& w, bool closed) {
			auto nodes = std::exchange(w.pen, {});
			w.hover.reset();
			if (nodes.size() < 2) return;
			auto [points, b] = reshaped_as(model::pen_path(nodes, closed));
			draw_layer(p, spec, layers, b, points);
		}

		// Words pasted: a layer of them in the middle, a twelfth of its height.
		void add_words(page& p, const collage_spec& spec, const value& layers, double width, double height, const std::string& words) {
			if (spec.add.empty() || !spec.fields.contains("words")) return;
			if (!layer_ready(p, spec, [&p, spec, layers, width, height, words] { add_words(p, spec, layers, width, height, words); })) return;
			double size = std::max(8.0, std::round(height / 12));
			auto [mw, mh] = measured(words, size, 700, false);
			value made = value(fields{{"words", words}, {"size", size}, {"x", (width - mw) / 2}, {"y", (height - mh) / 2}, {"w", mw}, {"h", mh}});
			if (!spec.parent.empty()) made.fields[spec.parent] = value(spec.subject);
			in_layer(p, spec, made);
			const auto& all = layers.as_array();
			if (!all.empty() && !all.back()["depth"].is_null()) made.fields["depth"] = value(all.back()["depth"].as_double() + 1);
			p.run(spec.add, collage_fields_of(made, spec.fields));
		}

		// What's on the clipboard, pasted: files a file manager copied, or a picture, as
		// pictures; or words.
		void paste(page& p, const collage_spec& spec, const value& layers, double width, double height) {
			// What this copied, still on the clipboard, comes back as itself.
			if (auto words = desktop::clipboard_text(); words && !copied().rows.empty() && *words == copied().svg) return paste_objects(p, spec, layers);
			if (auto files = desktop::clipboard_files(); !files.empty()) take_pictures(p, spec, files, std::nullopt);
			else if (auto bytes = desktop::clipboard_picture()) take_pictures(p, spec, {kept_for_now(*bytes)}, std::nullopt);
			else if (auto words = desktop::clipboard_text(); words && words->find_first_not_of(" \n\t") != std::string::npos) {
				// An SVG's text, as a drawing program copies one, is that SVG, as one dropped is.
				std::vector<std::uint8_t> bytes(words->begin(), words->end());
				if (desktop::picture::is_svg(bytes)) take_pictures(p, spec, {kept_for_now(bytes)}, std::nullopt);
				else add_words(p, spec, layers, width, height, *words);
			}
			else p.told().set("There's nothing to paste. Copy a picture or some words first.");
		}

		// With around harmonize, what's under the collage's pictures filled from all the
		// harmonized ones together (harmonic fill): each drawn small where it is, the one on
		// top where they overlap, and every gap settled between the colors around it, so
		// past the outermost each one's colors carry on to the edge, and between two they
		// cross from one to the other. Drawn as big as the collage, smooth as it is, under
		// all of them; worked out again only when one of them, or where it is, changes.
		desktop::picture harmonized(const std::vector<std::pair<model::box, look>>& under, double width, double height) {
			if (under.empty() || width <= 0 || height <= 0) return {};
			std::string key = std::format("{} {}", width, height);
			for (const auto& [b, seen] : under) key += std::format(" {} {} {} {} {}", seen.key, b.x, b.y, b.w, b.h);
			static kept_by<desktop::picture> made(8);
			return made.get(key, [&] {
				int gw = 320, gh = std::max(1, static_cast<int>(std::lround(320 * height / width)));
				double s = gw / width;
				std::size_t cells = static_cast<std::size_t>(gw) * gh;
				std::vector<double> colors(cells * 3, 0);
				std::vector<bool> covered(cells, false);
				for (const auto& [b, seen] : under) {
					// The cells a picture covers whole are its color; one its edge crosses is
					// part picture, part nothing, and would leave a seam of the two.
					int left = std::max(0, static_cast<int>(std::ceil(b.x * s))), right = std::min(gw, static_cast<int>(std::floor((b.x + b.w) * s)));
					int top = std::max(0, static_cast<int>(std::ceil(b.y * s))), bottom = std::min(gh, static_cast<int>(std::floor((b.y + b.h) * s)));
					if (right <= left || bottom <= top || !seen.picture) continue;
					model::box part{(left / s - b.x) / b.w, (top / s - b.y) / b.h, (right - left) / s / b.w, (bottom - top) / s / b.h};
					auto small = averaged(small_of(seen), part, right - left, bottom - top);
					for (int y = top; y < bottom; ++y) {
						for (int x = left; x < right; ++x) {
							std::size_t at = static_cast<std::size_t>(y) * gw + x, from = (static_cast<std::size_t>(y - top) * (right - left) + (x - left)) * 3;
							covered[at] = true;
							for (std::size_t c = 0; c < 3; ++c) colors[at * 3 + c] = small[from + c];
						}
					}
				}
				colors = model::harmonic_fill(gw, gh, std::move(colors), covered);
				std::vector<std::uint8_t> filled(cells * 4, 255);
				for (std::size_t i = 0; i < cells; ++i) {
					for (std::size_t c = 0; c < 3; ++c) filled[i * 4 + c] = static_cast<std::uint8_t>(std::clamp(std::lround(colors[i * 3 + c]), 0L, 255L));
				}
				return desktop::picture::of(gw, gh, std::move(filled));
			});
		}

		// What each picture's colors are moved by while the collage matches them, so they
		// look like one picture: each moved toward all of theirs, weighed by how much of
		// the collage it covers, so the larger lead; from its colors as `measure` says
		// them; while one's aren't known yet, nothing's matched.
		template <class Measure>
		std::map<std::string, model::color_shift> shifts_of(page& p, const value& layers, bool matching, Measure measure, bool* unknown = nullptr) {
			std::map<std::string, model::color_shift> out;
			if (!matching) return out;
			std::vector<std::string> ids;
			std::vector<model::color_stats> stats;
			for (const auto& l : layers.as_array()) {
				if (!is_picture(l)) continue;
				auto file = p.file_here(l["picture"].as_string());
				model::box b = box_of(l);
				if (!file || b.w <= 0 || b.h <= 0) continue;
				std::optional<model::color_stats> known = measure(*file, l, b.w * b.h);
				if (!known) {
					if (unknown) *unknown = true;
					return out;
				}
				ids.push_back(l["id"].as_string());
				stats.push_back(*known);
			}
			auto shifts = model::matched(stats, model::matching, model::matching_cast);
			for (std::size_t i = 0; i < ids.size(); ++i) out[ids[i]] = shifts[i];
			return out;
		}

		// Colors measured now, as a picture saved is drawn; or only as they're known
		// already, as the window draws, which never waits on reading a picture.
		std::optional<model::color_stats> measured_now(const std::filesystem::path& file, const value& layer, double weight) {
			look seen = kept_look(file, layer);
			if (!seen.picture) return std::nullopt;
			return stats_kept(seen, weight);
		}
		std::optional<model::color_stats> measured_already(const std::filesystem::path& file, const value& layer, double weight) {
			auto known = measured_colors.peek(kept_key(file, layer));
			if (known) known->weight = weight;
			return known;
		}

		// As known already, and what isn't measured away from the window's thread, once,
		// `woken` changed when it is so the window draws again: so a matched picture is
		// made ready once, with its colors moved, not first as it was.
		template <class Woken>
		auto measured_soon(Woken woken) {
			return [woken](const std::filesystem::path& file, const value& layer, double weight) {
				auto known = measured_already(file, layer, weight);
				if (known) return known;
				static std::mutex measuring;
				static std::set<std::string> working;
				std::string key = kept_key(file, layer);
				std::lock_guard hold(measuring);
				if (working.insert(key).second) {
					in_background([file, layer, key, woken] {
						(void)measured_now(file, layer, 1);
						{
							std::lock_guard hold(measuring);
							working.erase(key);
						}
						post([woken] { woken.set(woken.peek() + 1); });
					});
				}
				return known;
			};
		}

		// All a picture layer is drawn from: its look and its halo; its colors measured on
		// the way, so matching finds them known.
		struct ready_layer {
			look seen;
			std::optional<halo_drawn> halo;
		};

		// In the window, a picture whose colors are moved is moved no larger than this,
		// as it's never shown larger; saved, it's moved whole.
		constexpr int shown_largest = 2048;

		ready_layer made_ready(const std::filesystem::path& file, const value& layer, std::optional<model::color_shift> shift, int largest = 0) {
			if (look kept = kept_look(file, layer); kept.picture) (void)stats_kept(kept, 1);
			look seen = look_at(file, layer, shift ? &*shift : nullptr, largest);
			return {seen, halo_of(layer, seen)};
		}

		// A picture layer as the window draws it: ready, or made ready away from the
		// window's thread and shown once it is, `woken` changed then so the window draws
		// again; meanwhile, the last made ready for that layer, or nothing yet. With no
		// window to wake, as when it's drawn to a file, it's made ready now. While
		// `last_only`, as while the colors it would be matched by are being measured, only
		// the last made ready is shown, and nothing's made.
		template <class Woken>
		std::optional<ready_layer> ready_of(page& p, const value& layer, const std::map<std::string, model::color_shift>& shifts, double width, double height, Woken woken,
		                                    bool last_only = false) {
			auto file = p.file_here(layer["picture"].as_string());
			if (!file || !vector_of(*file).empty()) return std::nullopt;
			std::optional<model::color_shift> shift;
			if (auto found = shifts.find(layer["id"].as_string()); found != shifts.end()) shift = found->second;
			if (!posting()) return made_ready(*file, layer, shift);
			model::box b = box_of(layer);
			std::string key = std::format("{} {} {} {} {} {} {} {}", kept_key(*file, layer), layer["around"].as_string(), b.x, b.y, b.w, b.h, width, height);
			if (shift) {
				key += std::format(" {:.3f} {:.3f} {:.3f} {:.1f} {:.1f} {:.1f}", shift->gain[0], shift->gain[1], shift->gain[2], shift->offset[0], shift->offset[1], shift->offset[2]);
				for (double t : shift->to) key += std::format(" {:.1f}", t);
			}
			static kept_by<ready_layer> readied(256);
			static std::mutex preparing;
			static std::set<std::string> working;
			static std::map<std::string, std::string> last;  // each layer's last made ready, by its id
			std::string id = layer["id"].as_string();
			if (auto known = readied.peek(key)) {
				std::lock_guard hold(preparing);
				last[id] = key;
				return known;
			}
			std::string shown;
			{
				std::lock_guard hold(preparing);
				if (!last_only && working.insert(key).second) {
					in_background([key, id, file = *file, layer, shift, width, height, woken] {
						ready_layer made = made_ready(file, layer, shift, shown_largest);
						readied.get(key, [&] { return made; });
						{
							std::lock_guard hold(preparing);
							working.erase(key);
							last[id] = key;
						}
						post([woken] { woken.set(woken.peek() + 1); });
					});
				}
				if (auto found = last.find(id); found != last.end()) shown = found->second;
			}
			return shown.empty() ? std::nullopt : readied.peek(shown);
		}

		// A shape layer drawn at `on`, its box `b` in the collage's pixels: from its SVG, by
		// the pinned nanosvg, as many pixels as it's shown, and as much more around as its
		// stroke reaches; made once for each look and size.
		void paint_shape(desktop::canvas& c, desktop::rect on, model::box b, const value& layer) {
			if (b.w <= 0 && b.h <= 0) return;
			model::shape s{layer["shape"].as_string().empty() ? "rectangle" : layer["shape"].as_string(), layer["points"].as_string(), layer["fill"].as_string(),
			               layer["stroke"].as_string(), layer["stroke"].as_string().empty() ? 0 : layer["stroke_width"].as_double(), layer["corners"].as_double(),
			               layer["sides"].is_null() ? 5 : layer["sides"].as_double(), layer["spoke"].is_null() ? 0.5 : layer["spoke"].as_double()};
			double per = b.w > 0 ? on.width / b.w : on.height / b.h;  // window pixels a collage pixel
			double reach = s.stroke_width / 2 + 1;
			double device = per * c.scale();
			int pw = std::max(1, static_cast<int>(std::lround((b.w + 2 * reach) * device))), ph = std::max(1, static_cast<int>(std::lround((b.h + 2 * reach) * device)));
			std::string svg = model::svg_alone(s, b.w, b.h, reach);
			static kept_by<desktop::picture> made(128);
			desktop::picture drawn = made.get(std::format("{} {} {}", svg, pw, ph), [&] { return desktop::picture::from_svg(svg, pw, ph); });
			if (!drawn) return;
			float out = static_cast<float>(reach * per);
			c.picture(drawn, {on.x - out, on.y - out, on.width + 2 * out, on.height + 2 * out}, {0, 0, 1, 1}, opacity_of(layer));
		}

		// Where a clear collage has nothing, a checkerboard, as drawing programs show
		// what's clear: white and gray, or in a dark theme, the theme's own blue-grays;
		// squares 8 window pixels on a side, each on whole pixels so its edges are sharp.
		void paint_checkers(desktop::canvas& c, desktop::rect on) {
			bool dark = desktop::dark(current_theme());
			desktop::color light = desktop::color::hex(dark ? 0x2b3242 : 0xffffff), shade = desktop::color::hex(dark ? 0x1a1f2b : 0xcccccc);
			auto kept = c.clip(on);
			c.box(on, {.fill = light});
			float s = static_cast<float>(c.scale());
			int side = std::max(1, static_cast<int>(std::lround(8 * s)));
			int left = static_cast<int>(std::floor(on.x * s)), top = static_cast<int>(std::floor(on.y * s));
			int right = static_cast<int>(std::ceil(on.right() * s)), bottom = static_cast<int>(std::ceil(on.bottom() * s));
			for (int y = top, row = 0; y < bottom; y += side, ++row) {
				for (int x = left + (row % 2 ? 0 : side); x < right; x += 2 * side) {
					c.box({x / s, y / s, side / s, side / s}, {.fill = shade});
				}
			}
		}

		// An SVG layer drawn at `on`, from its vectors, by the pinned nanosvg, as many
		// pixels as it's shown; made once for each size. Stretched to its box when that's
		// shaped otherwise than it is.
		void paint_vector(desktop::canvas& c, desktop::rect on, const std::string& svg, const value& layer) {
			auto size = desktop::picture::svg_size(svg);
			if (!size || on.width <= 0 || on.height <= 0) return;
			int pw = std::max(1, static_cast<int>(std::lround(on.width * c.scale())));
			int ph = std::max(1, static_cast<int>(std::lround(pw * size->second / size->first)));
			static kept_by<desktop::picture> made(64);
			desktop::picture drawn = made.get(std::format("{} {} {}", std::hash<std::string>{}(svg), pw, ph), [&] { return desktop::picture::from_svg(svg, pw, ph); });
			if (drawn) c.picture(drawn, on, {0, 0, 1, 1}, opacity_of(layer));
		}

		// A picture layer drawn at `on` in the window, `board` the collage there: what's
		// around it first, as its around says, over what's under it; then it, its corners
		// trimmed round as far as its corners say, a share of its shorter side; then,
		// when it's harmonized, its halo. Without `effects`, as while something's being
		// moved or cropped, it alone.
		void paint_picture(desktop::canvas& c, desktop::rect board, desktop::rect on, const value& layer, const look& seen, const std::optional<halo_drawn>& halo,
		                   bool effects = true) {
			std::string around = layer["around"].as_string();
			const desktop::picture& kept = seen.picture;
			if (effects && (around == "edges" || around == "color")) paint_around(c, board, on, seen.key, kept, around);
			c.picture(kept, on, {0, 0, 1, 1}, opacity_of(layer), static_cast<float>(layer["corners"].as_double()) * std::min(on.width, on.height));
			if (effects && halo) paint_halo(c, *halo, on);
		}

		// What's under the harmonized pictures of those ready, in the order they're drawn.
		std::vector<std::pair<model::box, look>> harmonized_of(const value& layers, const std::map<std::string, ready_layer>& ready) {
			std::vector<std::pair<model::box, look>> out;
			for (const auto& l : layers.as_array()) {
				if (!is_picture(l) || !harmonizes(l)) continue;
				if (auto found = ready.find(l["id"].as_string()); found != ready.end() && found->second.seen.picture) out.emplace_back(box_of(l), found->second.seen);
			}
			return out;
		}

		// What drawing it whole needs, gathered on the window's thread: its pictures, read,
		// turned and cropped, what each fills around itself, and the font its words are in,
		// so it's drawn away from that thread.
		struct whole {
			value layers;
			double width = 0, height = 0, scale = 1;
			std::string only, family;
			bool clear = false;  // nothing behind its layers
			std::map<std::string, ready_layer> pictures;  // each picture layer's, by its id
			std::map<std::string, std::string> vectors;   // and each SVG layer's text, by its id
			desktop::picture fill;                        // and what's under the harmonized ones
		};

		whole gathered(page& p, const value& layers, double width, double height, double scale, bool matching, const std::string& only = "") {
			whole all{layers, width, height, scale, only, current_theme().text_family, false, {}, {}, {}};
			auto shifts = shifts_of(p, layers, matching, measured_now);
			for (const auto& l : layers.as_array()) {
				if (!is_picture(l)) continue;
				auto file = p.file_here(l["picture"].as_string());
				if (!file) continue;
				if (std::string svg = vector_of(*file); !svg.empty()) {
					all.vectors.emplace(l["id"].as_string(), std::move(svg));
					continue;
				}
				std::optional<model::color_shift> shift;
				if (auto found = shifts.find(l["id"].as_string()); found != shifts.end()) shift = found->second;
				all.pictures.emplace(l["id"].as_string(), made_ready(*file, l, shift));
			}
			if (only.empty()) all.fill = harmonized(harmonized_of(layers, all.pictures), width, height);
			return all;
		}

		// The collage drawn on nothing, `scale` times its own size, as it's kept: all of
		// it, or only one layer, as big as that layer, cropped as it is.
		desktop::picture drawn_whole(const whole& all) {
			const value& layers = all.layers;
			double width = all.width, height = all.height, scale = all.scale;
			model::box area{0, 0, width, height};
			for (const auto& l : layers.as_array()) {
				if (!all.only.empty() && l["id"].as_string() == all.only) area = box_of(l);
			}
			int w = std::max(1, static_cast<int>(std::lround(area.w * scale))), h = std::max(1, static_cast<int>(std::lround(area.h * scale)));
			return desktop::picture::drawn(w, h, [&](desktop::canvas& c) {
				auto at = [&](const model::box& b) {
					return desktop::rect{static_cast<float>((b.x - area.x) * scale), static_cast<float>((b.y - area.y) * scale), static_cast<float>(b.w * scale), static_cast<float>(b.h * scale)};
				};
				desktop::rect board = at({0, 0, width, height});
				if (all.only.empty() && !all.clear) c.box(board, {.fill = desktop::color::hex(0xffffff)});
				if (all.fill) c.picture(all.fill, board, {0, 0, 1, 1});
				for (const auto& layer : layers.as_array()) {
					std::string id = layer["id"].as_string();
					if (!all.only.empty() && id != all.only) continue;
					if (is_words(layer)) {
						desktop::rect on = at(box_of(layer));
						paint_words(c, layer, layer["words"].as_string(), layer["size"].as_double() * scale, {on.x, on.y}, std::string::npos, all.family);
						continue;
					}
					if (is_shape(layer)) {
						paint_shape(c, at(box_of(layer)), box_of(layer), layer);
						continue;
					}
					if (auto svg = all.vectors.find(id); svg != all.vectors.end()) {
						paint_vector(c, at(box_of(layer)), svg->second, layer);
						continue;
					}
					auto found = all.pictures.find(id);
					if (found == all.pictures.end() || !found->second.seen.picture) continue;
					const ready_layer& ready = found->second;
					paint_picture(c, board, at(box_of(layer)), layer, ready.seen, ready.halo);
				}
			});
		}

		// Where saved pictures go: the app's own folder in the Pictures folder the desktop
		// names, or ~/Pictures.
		std::filesystem::path saving_folder() {
			const char* home = std::getenv("HOME");
			std::filesystem::path pictures = std::filesystem::path(home && *home ? home : "/tmp") / "Pictures";
			if (const char* named = std::getenv("XDG_PICTURES_DIR"); named && *named) pictures = named;
			else {
				const char* config = std::getenv("XDG_CONFIG_HOME");
				std::filesystem::path dirs = (config && *config ? std::filesystem::path(config) : std::filesystem::path(home ? home : "") / ".config") / "user-dirs.dirs";
				std::ifstream in(dirs);
				for (std::string line; std::getline(in, line);) {
					if (!line.starts_with("XDG_PICTURES_DIR=\"")) continue;
					std::string named = line.substr(18, line.rfind('"') - 18);
					if (named.starts_with("$HOME")) named = std::string(home ? home : "") + named.substr(5);
					if (!named.empty()) pictures = named;
				}
			}
			std::string app = app_name();
			return pictures / (app.empty() ? "pictures" : app);
		}

		// A name as a file's: lowercase words joined by hyphens.
		std::string file_name_of(const std::string& name) {
			std::string out;
			for (unsigned char ch : name) {
				if (std::isalnum(ch)) out += static_cast<char>(std::tolower(ch));
				else if (!out.empty() && out.back() != '-') out += '-';
			}
			while (!out.empty() && out.back() == '-') out.pop_back();
			return out.empty() ? "picture" : out;
		}

		// A picture saved as a PNG in the app's folder in Pictures, named for what the
		// view calls it and the minute, never over one saved before, written away from
		// the window; where it went is said once it's there.
		// Saved where pictures are, drawn and written away from the window's thread.
		void save(page& p, whole all, const std::string& name) {
			std::filesystem::path folder = saving_folder();
			std::string stem = std::format("{}-{:%Y-%m-%d-%H%M}", file_name_of(name),
			                               std::chrono::zoned_time(std::chrono::current_zone(), std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now())));
			std::filesystem::path file = folder / (stem + ".png");
			for (int n = 2; std::filesystem::exists(file); ++n) file = folder / std::format("{}-{}.png", stem, n);
			p.told().set("Saving…");
			in_background([told = p.told(), problem = p.problem(), about = p.kept<std::pair<std::string, std::string>>("said of a file"), all = std::move(all), folder, file] {
				std::error_code error;
				std::filesystem::create_directories(folder, error);
				auto written = drawn_whole(all).write_png(file);
				handed_back([told, problem, about, file, written] {
					if (!written) return told.set(""), problem.set(written.error());
					std::string said = std::format("Saved {} in {}.", file.filename().string(), file.parent_path().string());
					about.set({said, file.string()});
					told.set(said);
				});
			});
		}

		// Bytes as base64, as a data: address holds them.
		std::string base64(const std::string& bytes) {
			static constexpr char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			std::string out;
			out.reserve((bytes.size() + 2) / 3 * 4);
			for (std::size_t i = 0; i < bytes.size(); i += 3) {
				std::uint32_t n = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i])) << 16;
				if (i + 1 < bytes.size()) n |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i + 1])) << 8;
				if (i + 2 < bytes.size()) n |= static_cast<unsigned char>(bytes[i + 2]);
				out += digits[(n >> 18) & 63];
				out += digits[(n >> 12) & 63];
				out += i + 1 < bytes.size() ? digits[(n >> 6) & 63] : '=';
				out += i + 2 < bytes.size() ? digits[n & 63] : '=';
			}
			return out;
		}

		// A picture's file as a data: address, its kind told from its first bytes.
		std::string data_address(const std::filesystem::path& file) {
			std::ifstream in(file, std::ios::binary);
			std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			std::string kind = bytes.starts_with("\x89PNG") ? "image/png"
			                   : bytes.starts_with("\xFF\xD8") ? "image/jpeg"
			                   : bytes.starts_with("GIF8") ? "image/gif"
			                   : bytes.starts_with("BM") ? "image/bmp"
			                   : bytes.size() > 12 && bytes.substr(8, 4) == "WEBP" ? "image/webp"
			                                                                       : "application/octet-stream";
			return "data:" + kind + ";base64," + base64(bytes);
		}

		// The drawing as a saved SVG, from its vectors: all of it, or only one layer, as
		// big as that layer, a group's all it holds.
		struct svg_parts {
			model::box area;
			std::vector<model::saved_layer> saved;  // each picture's address, until it's written, its file
		};
		svg_parts svg_of(page& p, const value& layers, double width, double height, const std::vector<std::string>& only) {
			std::vector<model::saved_layer> saved;
			model::box area{0, 0, width, height};
			placed groups;
			std::vector<model::box> around;
			for (const auto& l : layers.as_array()) {
				if (std::ranges::contains(only, l["id"].as_string())) around.push_back(box_of(l));
				if (std::string group = l["group"].as_string(); !group.empty()) groups.group_of[l["id"].as_string()] = group;
			}
			if (!only.empty() && !around.empty()) area = model::around(around);
			for (const auto& l : layers.as_array()) {
				if (holds_others(l)) continue;
				std::string id = l["id"].as_string();
				if (!only.empty() && std::ranges::none_of(only, [&](const std::string& o) { return o == id || groups.inside(id, o); })) continue;
				model::box b = box_of(l);
				model::saved_layer one{.at = {b.x - area.x, b.y - area.y, b.w, b.h}, .opacity = opacity_of(l)};
				if (is_words(l)) {
					one.kind = "words", one.words = l["words"].as_string(), one.color = l["color"].as_string(), one.family = current_theme().text_family;
					one.size = l["size"].as_double(), one.weight = weight_of(l), one.italic = italic_of(l);
				} else if (is_shape(l)) {
					one.kind = "shape";
					one.drawn = {l["shape"].as_string().empty() ? "rectangle" : l["shape"].as_string(), l["points"].as_string(), l["fill"].as_string(), l["stroke"].as_string(),
					             l["stroke"].as_string().empty() ? 0 : l["stroke_width"].as_double(), l["corners"].as_double(),
					             l["sides"].is_null() ? 5 : l["sides"].as_double(), l["spoke"].is_null() ? 0.5 : l["spoke"].as_double()};
				} else if (std::string svg = vector_here(p, l); !svg.empty()) {
					one.kind = "svg", one.svg = svg;
					if (auto size = desktop::picture::svg_size(svg)) one.natural = {0, 0, size->first, size->second};
				} else if (auto file = p.file_here(l["picture"].as_string())) {
					one.kind = "picture", one.href = file->string(), one.crop = crop_of(l);
					one.turns = static_cast<int>(l["turns"].as_double()), one.mirrored = l["mirrored"].as_double() != 0, one.corners = l["corners"].as_double();
				} else {
					continue;
				}
				saved.push_back(std::move(one));
			}
			return {area, std::move(saved)};
		}

		// Those parts written, each picture's file read into its address: on any thread.
		std::string svg_written(svg_parts parts) {
			for (auto& one : parts.saved) {
				if (one.kind == "picture") one.href = data_address(one.href);
			}
			return model::svg_document(parts.area.w, parts.area.h, parts.saved);
		}

		// A drawing saved as an SVG in the app's folder in Pictures, as a picture is.
		void save_svg(page& p, svg_parts parts, const std::string& name) {
			std::filesystem::path folder = saving_folder();
			std::string stem = std::format("{}-{:%Y-%m-%d-%H%M}", file_name_of(name),
			                               std::chrono::zoned_time(std::chrono::current_zone(), std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now())));
			std::filesystem::path file = folder / (stem + ".svg");
			for (int n = 2; std::filesystem::exists(file); ++n) file = folder / std::format("{}-{}.svg", stem, n);
			p.told().set("Saving…");
			in_background([told = p.told(), problem = p.problem(), about = p.kept<std::pair<std::string, std::string>>("said of a file"), parts = std::move(parts), folder, file] {
				std::error_code error;
				std::filesystem::create_directories(folder, error);
				std::ofstream out(file, std::ios::binary);
				out << svg_written(parts);
				bool written = static_cast<bool>(out);
				handed_back([told, problem, about, folder, file, written] {
					if (!written) return told.set(""), problem.set("The drawing couldn't be saved in " + folder.string() + ".");
					std::string said = std::format("Saved {} in {}.", file.filename().string(), file.parent_path().string());
					about.set({said, file.string()});
					told.set(said);
				});
			});
		}

		// What a collage's preview is drawn from, apart from the preview itself.
		std::string fingerprint(const value& data, const std::string& preview_into) {
			value plain = data;
			plain.fields.erase(preview_into);
			return std::format("{:016x}", std::hash<std::string>{}(ember::to_json(plain)));
		}

	}  // namespace

	// Where a picture's bytes are kept until they're sent: this person's own runtime
	// folder, readable by them alone.
	std::filesystem::path kept_for_now(const std::vector<std::uint8_t>& bytes) {
		static int made = 0;
		const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
		std::filesystem::path folder = runtime_dir && *runtime_dir ? std::filesystem::path(runtime_dir) : std::filesystem::temp_directory_path();
		std::filesystem::path file = folder / std::format("one-picture-{}-{}", ::getpid(), ++made);
		std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		std::filesystem::permissions(file, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
		return file;
	}

	std::string picture_name(const std::filesystem::path& file) {
		std::string name = file.stem().string();
		return name.empty() || name.starts_with("one-picture-") ? "Pasted image" : name;
	}

	std::optional<desktop::picture> picture_soon(page& p, const std::string& kept, int longest) {
		auto file = kept.empty() ? std::nullopt : p.file_here(kept);
		return file ? read_soon(*file, longest) : std::optional(desktop::picture());
	}

	namespace {
		// A file's picture to draw now: the whole of it, wanted for as long as the page
		// is open, or a copy no longer than `longest`; until it's read, the copy a card
		// drew of it, when there's one.
		desktop::picture shown_now(page& p, const std::string& kept, int longest) {
			auto file = kept.empty() ? std::nullopt : p.file_here(kept);
			std::optional<std::weak_ptr<void>> wanted;
			if (longest <= 0) wanted = p.kept<std::shared_ptr<void>>("pictures wanted", std::make_shared<bool>()).peek();
			auto read = file ? read_soon(*file, longest, wanted) : std::optional(desktop::picture());
			if (!read && longest <= 0) read = pictures_kept(640).peek(std::format("{} {}", file->string(), 640));
			return read.value_or(desktop::picture());
		}

		// How a picture alone is looked at: how many times larger than fitted, the point
		// of it in the middle of the screen, in fractions of its width and height, and
		// where it was last drawn, as large as it was then, for what the pointer does.
		struct looked_at {
			float times = 1;
			float x = 0.5f, y = 0.5f;
			desktop::rect area;
			float wide = 0, high = 0;
			double scale = 1;
			std::optional<desktop::point> held;  // where a drag is, while one is
		};

		// Where it's drawn: fitted, then as many times larger as it's zoomed, its point in
		// the middle, but never past its edges once it's larger than the screen.
		desktop::rect drawn_at(const looked_at& l) {
			const desktop::rect& a = l.area;
			if (l.wide <= 0 || l.high <= 0) return a;
			float s = std::min(a.width / l.wide, a.height / l.high) * l.times;
			float w = l.wide * s, h = l.high * s;
			float x = a.x + a.width / 2 - l.x * w, y = a.y + a.height / 2 - l.y * h;
			x = w <= a.width ? a.x + (a.width - w) / 2 : std::clamp(x, a.x + a.width - w, a.x);
			y = h <= a.height ? a.y + (a.height - h) / 2 : std::clamp(y, a.y + a.height - h, a.y);
			return {x, y, w, h};
		}

		// Its point in the middle, as it's drawn, so a drag past an edge doesn't build up.
		void settled(looked_at& l) {
			desktop::rect r = drawn_at(l);
			if (r.width <= 0 || r.height <= 0) return;
			l.x = (l.area.x + l.area.width / 2 - r.x) / r.width;
			l.y = (l.area.y + l.area.height / 2 - r.y) / r.height;
		}

		// How many times larger than fitted it's drawn at 100%, a pixel of it a pixel of
		// the screen's.
		float actual(const looked_at& l) {
			if (l.wide <= 0 || l.high <= 0 || l.area.width <= 0 || l.area.height <= 0) return 1;
			return static_cast<float>(1 / (std::min(l.area.width / l.wide, l.area.height / l.high) * l.scale));
		}

		// Zoomed by `by` about a point, which stays where it is: never smaller than fitted,
		// nor larger than eight times 100%.
		void zoomed(looked_at& l, float by, desktop::point at) {
			desktop::rect before = drawn_at(l);
			if (before.width <= 0 || before.height <= 0) return;
			float fx = (at.x - before.x) / before.width, fy = (at.y - before.y) / before.height;
			l.times = std::clamp(l.times * by, 1.f, std::max(4.f, 8 * actual(l)));
			if (l.times <= 1.0001f) {
				l.times = 1, l.x = l.y = 0.5f;
				return;
			}
			float s = std::min(l.area.width / l.wide, l.area.height / l.high) * l.times;
			l.x = fx + (l.area.x + l.area.width / 2 - at.x) / (l.wide * s);
			l.y = fy + (l.area.y + l.area.height / 2 - at.y) / (l.high * s);
			settled(l);
		}

		void moved(looked_at& l, float dx, float dy) {
			desktop::rect r = drawn_at(l);
			if (r.width <= 0 || r.height <= 0) return;
			l.x += dx / r.width, l.y += dy / r.height;
			settled(l);
		}

		desktop::point middle(const looked_at& l) { return {l.area.x + l.area.width / 2, l.area.y + l.area.height / 2}; }
	}  // namespace

	desktop::view picture_alone(page& p, const std::string& kept, const std::string& key) {
		p.alone();
		// Kept per page, so the next picture stepped to is fitted again; what's done to
		// it is drawn as the window is next, which what's done asks for.
		auto l = p.kept<std::shared_ptr<looked_at>>("alone " + key, std::make_shared<looked_at>()).peek();
		auto zoom = [l](float by) { zoomed(*l, by, middle(*l)); };
		auto fit = [l] { l->times = 1, l->x = l->y = 0.5f; };
		for (const char* k : {"ctrl+equal", "ctrl+plus"}) p.on_key(k, [zoom] { zoom(1.25f); });
		p.on_key("ctrl+minus", [zoom] { zoom(0.8f); });
		p.on_key("ctrl+0", fit);
		// What the screen's zoom buttons asked of it.
		if (auto asked = p.kept<std::string>("collage do " + key); !asked.get().empty()) {
			std::string does = asked.peek();
			asked.set("");
			if (does == "zoom in") zoomed(*l, 1.25f, middle(*l));
			else if (does == "zoom out") zoomed(*l, 0.8f, middle(*l));
			else if (does == "zoom to fit") fit();
		}
		desktop::picture shown = shown_now(p, kept, 0);
		bool shows_pointer = awake();
		return desktop::drawing(
		           [shown, l](desktop::canvas& c, desktop::rect area) {
			           // On black, as a photo is looked at, whatever the theme.
			           c.box(area, {.fill = desktop::color::hex(0x000000)});
			           l->area = area;
			           l->scale = c.scale();
			           if (!shown || area.width <= 0 || area.height <= 0) return;
			           l->wide = static_cast<float>(shown.width()), l->high = static_cast<float>(shown.height());
			           auto inside = c.clip(area);
			           c.picture(shown, drawn_at(*l));
		           },
		           {.on_press =
		                [l](desktop::point at, int clicks) {
			                // Twice, to 100% about where it's pressed, or back to fitted.
			                if (clicks == 2) {
				                if (l->times > 1.0001f) {
					                l->times = 1, l->x = l->y = 0.5f;
				                } else {
					                float to = actual(*l);
					                zoomed(*l, to > 1.01f ? to : 2.f, at);
				                }
				                return true;
			                }
			                if (l->times <= 1.0001f) return false;
			                l->held = at;
			                return true;
		                },
		            .on_drag =
		                [l](desktop::point at) {
			                if (!l->held) return;
			                moved(*l, l->held->x - at.x, l->held->y - at.y);
			                l->held = at;
			                desktop::runtime::redraw();
		                },
		            .on_release =
		                [l](desktop::point) {
			                l->held.reset();
		                },
		            // With Ctrl, the wheel zooms about the pointer, as a collage's does; without,
		            // it moves about a picture larger than fits, as a touchpad's two fingers do.
		            .on_wheel =
		                [l](desktop::point at, float dx, float dy) {
			                if (desktop::held().control) {
				                zoomed(*l, std::exp(-dy * 0.01f), at);
				                return true;
			                }
			                if (l->times <= 1.0001f) return false;
			                if (desktop::held().shift) std::swap(dx, dy);
			                moved(*l, dx, dy);
			                return true;
		                },
		            .on_pinch =
		                [l](desktop::point at, float by) {
			                zoomed(*l, by, at);
			                return true;
		                },
		            .cursor =
		                [l, shows_pointer](desktop::point) {
			                if (!shows_pointer) return desktop::cursor::hidden;
			                if (l->held) return desktop::cursor::grabbing;
			                return l->times > 1.0001f ? desktop::cursor::grab : desktop::cursor::normal;
		                }})
		    .grow();
	}

	desktop::view picture_of(page& p, const std::string& kept, bool fit, float height, int longest) {
		desktop::picture shown = shown_now(p, kept, longest);
		return desktop::drawing([shown, fit](desktop::canvas& c, desktop::rect area) {
			       const auto& t = current_theme();
			       c.box(area, {.fill = t.sunken});
			       if (!shown || area.width <= 0 || area.height <= 0) return;
			       float pw = static_cast<float>(shown.width()), ph = static_cast<float>(shown.height());
			       float s = fit ? std::min(area.width / pw, area.height / ph) : std::max(area.width / pw, area.height / ph);
			       if (fit) {
				       c.picture(shown, {area.x + (area.width - pw * s) / 2, area.y + (area.height - ph * s) / 2, pw * s, ph * s});
			       } else {
				       float fw = area.width / (pw * s), fh = area.height / (ph * s);
				       c.picture(shown, area, {(1 - fw) / 2, (1 - fh) / 2, fw, fh});
			       }
		       })
		    .height(height);
	}

	desktop::view collage_button(page& p, collage_ask ask) {
		auto asked = p.kept<std::string>("collage do " + ask.key);
		auto press = [asked, does = ask.does] { asked.set(does); };
		bool off = p.in_group() && p.off();
		if (!off) {
			for (const auto& k : ask.keys) p.on_key(k, press);
		}
		if (auto small = p.in_group()) {
			return desktop::tool_button(desktop::has_icon(ask.icon) ? ask.icon : "", *small ? hint_of(ask.label, ask.keys) : ask.label, std::move(press), {.large = !*small, .off = off});
		}
		return desktop::tool_button(desktop::has_icon(ask.icon) ? ask.icon : "", hint_of(ask.label, ask.keys), std::move(press));
	}

	desktop::view collage(page& p, collage_spec spec) {
		auto w = p.kept<std::shared_ptr<placed>>("collage " + spec.list, std::make_shared<placed>()).peek();
		const view_state& v = *spec.view;
		// While its view isn't there to show, what it says instead.
		if (v.status == status::loading) return desktop::text("Loading…", current_theme().quiet());
		if (v.status == status::failed) return desktop::text(v.error.empty() ? "This couldn't be read." : v.error, current_theme().quiet());
		// Read as its parts, whichever of the app's fields keep them.
		const value data = collage_parts_of(v.data, spec.list, spec.fields);
		// Those hidden aren't drawn, picked, snapped to or saved; held in layers, they're
		// in their layers' order, and those of a hidden layer aren't either.
		// A group is drawn as what it holds, its place the box around them.
		const value layers = grouped(spec.layers.empty() ? shown_layers(data[spec.list]) : in_layers(shown_layers(data[spec.list]), data[spec.layers], spec.layer));
		w->group_of.clear();
		for (const auto& l : layers.as_array()) {
			if (std::string group = l["group"].as_string(); !group.empty()) w->group_of[l["id"].as_string()] = group;
		}
		if (!spec.layers.empty()) {
			auto now = p.kept<std::string>("collage layer " + spec.key);
			std::string current = current_layer(data[spec.layers], spec.layer_pick.empty() ? "" : p.picked_id(spec.layer_pick));
			if (now.peek() != current) now.set(current);
			// And the topmost layer's depth, which a new one goes past.
			double top = 0;
			for (const auto& l : data[spec.layers].as_array()) top = std::max(top, l["depth"].as_double());
			auto kept_top = p.kept<double>("collage top layer " + spec.key);
			if (kept_top.peek() != top) kept_top.set(top);
		}
		double width = data[spec.width].as_double(), height = data[spec.height].as_double();
		std::string picked = spec.pick.empty() ? "" : p.picked_id(spec.pick);
		// Several chosen are let go as one's picked, as the layers' list may; and a group
		// gone into is left once it's gone, or what's picked is outside it.
		if (!picked.empty() && w->chosen.size() > 1) w->chosen.clear();
		std::erase_if(w->chosen, [&](const std::string& id) { return std::ranges::none_of(layers.as_array(), [&](const value& l) { return l["id"].as_string() == id; }); });
		if (!w->entered.empty() && std::ranges::none_of(layers.as_array(), [&](const value& l) { return l["id"].as_string() == w->entered; })) w->entered.clear();
		// What a style applies to: each of several chosen, or all a picked group holds.
		if (!spec.pick.empty()) {
			std::vector<std::string> styles;
			auto all_in = [&](const std::string& top) {
				for (const auto& l : layers.as_array()) {
					std::string id = l["id"].as_string();
					if (!holds_others(l) && (id == top || w->inside(id, top))) styles.push_back(id);
				}
			};
			if (w->chosen.size() > 1) {
				for (const auto& c : w->chosen) all_in(c);
			} else {
				for (const auto& l : layers.as_array()) {
					if (l["id"].as_string() == picked && holds_others(l)) all_in(picked);
				}
			}
			style_these(p, spec.pick, std::move(styles));
		}
		bool full = fullscreen();
		std::optional<value> chosen;
		for (const auto& l : layers.as_array()) {
			if (l["id"].as_string() == picked) chosen = l;
		}
		// A picture whose own size isn't kept yet, like one kept before its size was, has
		// it filled in from the picture as it's turned, once, never a step undo takes back.
		if (!spec.update.empty()) {
			auto asked = p.kept<std::shared_ptr<std::set<std::string>>>("collage sized " + spec.key, std::make_shared<std::set<std::string>>()).peek();
			for (const auto& l : layers.as_array()) {
				if (!l.fields.contains("picture_width") || l["picture_width"].as_double() > 0 || asked->contains(l["id"].as_string())) continue;
				auto file = l["picture"].as_string().empty() ? std::nullopt : p.file_here(l["picture"].as_string());
				auto size = file ? desktop::picture::size_of(*file) : std::nullopt;
				if (!size) continue;
				auto [sw, sh] = *size;
				if ((static_cast<int>(l["turns"].as_double()) % 2 + 2) % 2 == 1) std::swap(sw, sh);
				asked->insert(l["id"].as_string());
				p.run(spec.update, collage_fields_of(value(fields{{"id", l["id"]}, {"picture_width", sw}, {"picture_height", sh}, {"one:history", "apart"}}), spec.fields));
			}
		}
		// Its preview, drawn once changes have settled for a moment and nothing's being
		// moved, cropped or written, and sent never to be undone. One already there when
		// it's first shown is what it looks like now.
		if (!spec.preview.empty() && !spec.subject.empty() && width > 0 && height > 0) {
			std::string now = fingerprint(data, spec.preview_into);
			auto drawn_from = p.kept<std::string>("collage preview of " + spec.key);
			auto settled = p.kept<std::string>("collage settled " + spec.key), waiting = p.kept<std::string>("collage waiting " + spec.key);
			if (drawn_from.peek().empty() && (!data[spec.preview_into].as_string().empty() || layers.as_array().empty())) drawn_from.set(now);
			bool still = w->now.what == gesture::none && !w->crop && !w->editing;
			// Read so the timer's settling draws it, as it's set from away from here.
			if (now != drawn_from.peek() && still && settled.get() == now) {
				drawn_from.set(now);
				auto kept = gathered(p, layers, width, height, spec.preview_wide / width, spec.match_colors);
				kept.clear = spec.clear;
				// Drawn away from the window's thread, and sent once it's back.
				in_background([kept = std::move(kept), drawn = p.kept<coming_picture>("collage preview drawn " + spec.key), from = &p.source()] {
					coming_picture made{.file = kept_for_now(drawn_whole(kept).png())};
					if (auto k = from->keep(made.file); k && *k) made.kept = **k;
					handed_back([drawn, made] { drawn.set(made); });
				});
			} else if (now != drawn_from.peek() && waiting.peek() != now) {
				waiting.set(now);
				std::thread([settled, now] {
					std::this_thread::sleep_for(std::chrono::milliseconds(800));
					post([settled, now] { settled.set(now); });
				}).detach();
			}
		}
		if (auto drawn = p.kept<coming_picture>("collage preview drawn " + spec.key); !drawn.get().file.empty() && !spec.preview.empty()) {
			coming_picture made = drawn.peek();
			drawn.set({});
			value sent = value(fields{{"id", spec.subject}, {"one:history", "apart"}});
			if (!made.kept.empty()) p.run(spec.preview, with_uploaded(std::move(sent), spec.preview_into, {made.kept}), {}, [file = made.file](const answer&) { std::filesystem::remove(file); }, true);
			else p.run(spec.preview, std::move(sent), attached_files{{spec.preview_into, {made.file.string()}}}, [file = made.file](const answer&) { std::filesystem::remove(file); }, true);
		}
		// A crop or words are only of the picked layer; picking another puts them away.
		if (w->crop && w->crop->id != picked) w->crop.reset();
		if (w->editing && w->editing->id != picked) finish_writing(p, *w, layers, spec);
		// A screenshot taken, added like any picture.
		// Read so a screenshot handed back, or a button's ask, is acted on at once.
		if (auto shot = p.kept<std::string>("collage screenshot " + spec.key); !shot.get().empty()) {
			std::filesystem::path taken = shot.peek();
			shot.set("");
			take_pictures(p, spec, {taken}, std::nullopt);
		}
		// Pictures chosen from files, added like any dropped.
		if (auto chosen = p.kept<std::vector<std::string>>("collage chosen " + spec.key); !chosen.get().empty()) {
			std::vector<std::filesystem::path> files(chosen.peek().begin(), chosen.peek().end());
			chosen.set({});
			take_pictures(p, spec, files, std::nullopt);
		}
		if (auto come = pictures_come(p, spec.key); !come.empty()) add_pictures(p, spec, layers, width, height, come, p.kept<std::optional<model::point>>("collage pictures at " + spec.key).peek());
		// What its buttons asked of it: a crop begun, words written, or words added.
		if (auto asked = p.kept<std::string>("collage do " + spec.key); !asked.get().empty()) {
			std::string does = asked.peek();
			asked.set("");
			if (does == "crop" && chosen && is_picture(*chosen) && !spec.crop.empty()) {
				w->crop = crop_begun(*chosen);
			} else if (does == "ungroup" && chosen && holds_others(*chosen) && !spec.group.empty()) {
				// What it holds, each in no group, and it gone, all one step to undo.
				bool joined = false;
				for (const auto& l : layers.as_array()) {
					if (l["group"].as_string() != picked) continue;
					value sent = collage_fields_of(value(fields{{"id", l["id"]}, {"group", value()}}), spec.fields);
					if (std::exchange(joined, true)) sent.fields["one:history"] = value("joined");
					p.run(spec.group, sent);
				}
				if (!spec.remove.empty()) p.run(spec.remove, value(fields{{"id", picked}, {"one:history", "joined"}}));
				if (!spec.pick.empty()) p.pick(spec.pick, "");
			} else if (does == "group picked" && w->chosen.size() > 1 && !spec.add.empty() && !spec.group.empty()) {
				// A new group, as big as what's chosen, where the topmost of them is, and
				// each of them in it, all one step to undo; then it's picked.
				std::vector<value> members;
				for (const auto& l : layers.as_array()) {
					if (std::ranges::contains(w->chosen, l["id"].as_string())) members.push_back(l);
				}
				model::box b = chosen_box(*w, layers);
				value made = value(fields{{"x", b.x}, {"y", b.y}, {"w", b.w}, {"h", b.h}, {"depth", members.empty() ? 0.0 : members.back()["depth"].as_double()}});
				if (spec.fields.contains("name")) made.fields["name"] = value("Group");
				if (!spec.parent.empty()) made.fields[spec.parent] = value(spec.subject);
				if (!spec.layer.empty() && !members.empty()) made.fields[spec.layer] = members.back()[spec.layer];
				if (!w->entered.empty()) made.fields["group"] = value(w->entered);
				w->chosen.clear();
				p.run(spec.add, collage_fields_of(made, spec.fields), [&p, spec, members](const answer& a) {
					if (!a) return;
					for (const auto& m : members) {
						p.run(spec.group, collage_fields_of(value(fields{{"id", m["id"]}, {"group", *a}, {"one:history", "joined"}}), spec.fields));
					}
					if (!spec.pick.empty()) p.pick(spec.pick, *a);
				});
			} else if ((does == "raise" || does == "lower") && chosen && !spec.order.empty()) {
				// Past the one above it, or below, among those beside it, in one step.
				bool joined = false;
				for (const auto& [id, depth] : restacked(layers, picked, does == "raise", spec.layer)) {
					value sent = collage_fields_of(value(fields{{"id", id}, {"depth", depth}}), spec.fields);
					if (std::exchange(joined, true)) sent.fields["one:history"] = value("joined");
					p.run(spec.order, sent);
				}
			} else if (does == "zoom in" || does == "zoom out") {
				// About the middle of where it's shown.
				desktop::rect a = w->area;
				w->view = model::zoomed(w->view, {a.x, a.y, a.width, a.height}, width, height, w->margin, does == "zoom in" ? 1.25 : 0.8, {a.x + a.width / 2, a.y + a.height / 2});
			} else if (does == "zoom to fit") {
				w->view = {};
			} else if (does == "edit" && chosen && is_words(*chosen) && !spec.write.empty()) {
				w->editing = placed::writing{picked, (*chosen)["words"].as_string(), false};
			} else if (does == "paste") {
				paste(p, spec, layers, width, height);
			} else if (does == "screenshot" && spec.paste) {
				// The desktop's own screenshot, which waits on the person, away from the
				// window's thread; added like any picture once it's taken.
				// It's handed back through states, which outlive the page if it's left first.
				p.told().set("Taking a screenshot…");
				std::thread([told = p.told(), shot = p.kept<std::string>("collage screenshot " + spec.key)] {
					auto taken = desktop::screenshot();
					post([told, shot, taken] {
						if (!taken) return told.set(taken.error());
						told.set("");
						shot.set(taken->string());
					});
				}).detach();
			} else if (does == "add pictures" && spec.paste) {
				// The desktop's own file chooser, which waits on the person, away from the
				// window's thread, handed back as the screenshot is.
				std::thread([told = p.told(), chosen = p.kept<std::vector<std::string>>("collage chosen " + spec.key)] {
					auto files = desktop::chosen_files({.title = "Add pictures", .pictures = true, .several = true});
					post([told, chosen, files] {
						if (!files) return told.set(files.error());
						chosen.set(std::vector<std::string>(files->begin(), files->end()));
					});
				}).detach();
			} else if (does == "copy svg" || does == "copy svg picked" || does == "cut svg picked" || does == "save svg" || does == "save svg picked") {
				// As SVG, from its vectors: copied as its text, which drawing programs paste, or
				// saved. What's picked, several or one, is kept too, to be pasted back as itself.
				bool one = does.ends_with("picked"), copying = !does.starts_with("save"), cutting = does.starts_with("cut");
				std::vector<std::string> picks = w->chosen.size() > 1 ? w->chosen : picked.empty() ? std::vector<std::string>{} : std::vector<std::string>{picked};
				if (one && picks.empty()) p.told().set("Pick something first.");
				else if (layers.as_array().empty() || width <= 0 || height <= 0) p.told().set(copying ? "There's nothing to copy yet." : "There's nothing to save yet.");
				else if (copying) {
					// Written away from the window's thread, as pictures' files are read into it,
					// and put on the clipboard back on it.
					copied() = {};
					p.told().set("Copying…");
					in_background([told = p.told(), parts = svg_of(p, layers, width, height, one ? picks : std::vector<std::string>{}), one, cutting] {
						std::string svg = svg_written(parts);
						handed_back([told, svg, one, cutting] {
							desktop::copy_text(svg);
							if (one) copied().svg = svg;
							told.set(!one ? "Copied the drawing as SVG. Paste it anywhere." : cutting ? "Cut. Paste it into any layer, or anywhere as SVG." : "Copied. Paste it into any layer, or anywhere as SVG.");
						});
					});
					if (one) {
						for (const auto& l : layers.as_array()) {
							std::string id = l["id"].as_string();
							if (std::ranges::any_of(picks, [&](const std::string& o) { return o == id || w->inside(id, o); })) copied().rows.push_back(l);
						}
					}
					if (cutting && !spec.remove.empty()) {
						// Taken away, all of it, in one step.
						bool joined = false;
						for (const auto& l : copied().rows) {
							value sent = value(fields{{"id", l["id"]}});
							if (std::exchange(joined, true)) sent.fields["one:history"] = value("joined");
							p.run(spec.remove, sent);
						}
						w->chosen.clear();
						if (!spec.pick.empty()) p.pick(spec.pick, "");
					}
				} else {
					std::string name = data["title"].as_string().empty() ? data["name"].as_string() : data["title"].as_string();
					if (name.empty()) name = app_name();
					save_svg(p, svg_of(p, layers, width, height, one ? picks : std::vector<std::string>{}), one ? name + " layer" : name);
				}
			} else if (does == "copy" || does == "copy picked" || does == "save" || does == "save picked") {
				// Copied for another program to paste, or saved: all of it, or the picked layer.
				bool one = does.ends_with("picked"), copying = does.starts_with("copy");
				if (one && !chosen) p.told().set("Pick a layer first.");
				else if (layers.as_array().empty() || width <= 0 || height <= 0) p.told().set(copying ? "There's nothing to copy yet." : "There's nothing to save yet.");
				else {
					auto all = gathered(p, layers, width, height, 1, spec.match_colors, one ? picked : "");
					all.clear = spec.clear;
					if (copying) {
						// Drawn away from the window's thread, and put on the clipboard back on it.
						p.told().set("Copying…");
						in_background([told = p.told(), all = std::move(all), one] {
							auto drawn = drawn_whole(all);
							post([told, drawn, one] {
								desktop::copy_picture(drawn);
								told.set(one ? "Copied the layer. Paste it anywhere." : "Copied the picture. Paste it anywhere.");
							});
						});
					} else {
						// Named for what the view calls it, or for the app when it calls it nothing.
						std::string name = data["title"].as_string().empty() ? data["name"].as_string() : data["title"].as_string();
						if (name.empty()) name = app_name();
						save(p, std::move(all), one ? name + " layer" : name);
					}
				}
			} else if (does == "add" && !spec.add.empty() && spec.fields.contains("words")) {
				// Words in the middle, a twelfth of its height, all chosen so the first key
				// typed replaces them, on top of the rest.
				double size = std::max(8.0, std::round(height / 12));
				std::string words = "Your text";
				auto [mw, mh] = measured(words, size, 700, false);
				value made = value(fields{{"words", words}, {"size", size}, {"x", (width - mw) / 2}, {"y", (height - mh) / 2}, {"w", mw}, {"h", mh}});
				if (!spec.parent.empty()) made.fields[spec.parent] = value(spec.subject);
				in_layer(p, spec, made);
				const auto& all = layers.as_array();
				if (!all.empty() && !all.back()["depth"].is_null()) made.fields["depth"] = value(all.back()["depth"].as_double() + 1);
				p.run(spec.add, collage_fields_of(made, spec.fields), [&p, w, pick = spec.pick, words](const answer& a) {
					if (!a) return;
					if (!pick.empty()) p.pick(pick, *a);
					w->editing = placed::writing{*a, words, true};
				});
			}
		}
		// A path being drawn by a pen: Enter ends it, Esc drops it, Backspace takes back
		// its last node; put away when the pen is.
		if (!w->pen.empty()) {
			const collage_draw* d = drawing_now(spec);
			if (!d || d->by != "points") w->pen.clear(), w->hover.reset();
		}
		// Words typed are kept when the tool they're typed by is put away.
		if (w->editing && w->editing->unmade) {
			const collage_draw* d = drawing_now(spec);
			if (!d || d->by != "typing") finish_writing(p, *w, layers, spec);
		}
		if (!w->pen.empty()) {
			typing([&p, w, layers, spec](const key_event& k) {
				if (w->pen.empty()) return false;
				if (k.is(keys::enter)) pen_done(p, spec, layers, *w, false);
				else if (k.is(keys::escape)) w->pen.clear(), w->hover.reset();
				else if (k.is(keys::backspace)) w->pen.pop_back();
				else return false;
				return true;
			});
		}
		// Words being written take every key; a crop is kept with Enter and put away
		// with Esc, before anything else.
		if (w->editing) {
			typing([&p, w, layers, spec](const key_event& k) {
				if (!w->editing) return false;
				if (k.is(keys::escape)) {
					finish_writing(p, *w, layers, spec);
					return true;
				}
				return written(*w->editing, k);
			});
		} else if (w->crop) {
			p.on_key("enter", [&p, w, spec] { keep_crop(p, *w, spec); });
			p.on_key("escape", [w] { w->crop.reset(); });
		}
		// The buttons over the picked layer, as they are now; while cropping, keep and
		// put away instead.
		std::vector<std::pair<command_button, command_model>> over;
		if (!picked.empty() && !full && !w->crop && !w->editing) {
			for (const auto& b : spec.over) {
				if (auto shown = command_of(p, b)) {
					for (const auto& k : b.keys) p.on_key(k, [&p, spec, w, layers, name = b.command, fields = shown->fields] { run_over(p, spec, *w, layers, name, fields); });
					over.emplace_back(b, std::move(*shown));
				}
			}
		}
		// The keys of what asks the collage over the picked layer, while each fits it.
		if (chosen && !w->crop && !w->editing) {
			auto asked = p.kept<std::string>("collage do " + spec.key);
			for (const auto& a : spec.asks) {
				bool fits = a.does == "crop" ? is_picture(*chosen) : a.does == "edit" ? is_words(*chosen) : a.does == "ungroup" ? holds_others(*chosen) : true;
				if (!fits) continue;
				for (const auto& k : a.keys) p.on_key(k, [asked, does = a.does] { asked.set(does); });
			}
		}
		// While something's to be drawn and nothing's picked or under way, Esc sets the
		// choices its when reads back to their first, as a drawing program goes back to
		// its pointer.
		if (const collage_draw* d = drawing_now(spec); d && picked.empty() && w->pen.empty() && w->chosen.size() < 2 && !w->crop && !w->editing) {
			p.on_key("escape", [&p, back = d->back] {
				for (const auto& [key, first] : back) p.choose(key, first);
			});
		}
		// Several chosen: Esc lets them go, and Delete deletes them, a group with all it
		// holds, in one step.
		if (w->chosen.size() > 1 && !w->crop && !w->editing) {
			p.on_key("escape", [w] { w->chosen.clear(); });
			if (!spec.remove.empty()) {
				p.on_key("delete", [&p, w, layers, remove = spec.remove] {
					bool joined = false;
					for (const auto& l : layers.as_array()) {
						std::string id = l["id"].as_string();
						bool gone = std::ranges::any_of(w->chosen, [&](const std::string& c) { return c == id || w->inside(id, c); });
						if (!gone) continue;
						value sent = value(fields{{"id", id}});
						if (std::exchange(joined, true)) sent.fields["one:history"] = value("joined");
						p.run(remove, sent);
					}
					w->chosen.clear();
				});
			}
		}
		// Esc lets the picked layer go, or, in a group gone into, picks the group again;
		// the arrows nudge it, by ten with Shift.
		if (!picked.empty() && !w->crop && !w->editing) {
			p.on_key("escape", [&p, w, name = spec.pick] {
				p.pick(name, std::exchange(w->entered, ""));
			});
			if (chosen && !spec.place.empty()) {
				for (auto [key, dx, dy] : {std::tuple{"left", -1.0, 0.0}, {"right", 1.0, 0.0}, {"up", 0.0, -1.0}, {"down", 0.0, 1.0}}) {
					for (double by : {1.0, 10.0}) {
						std::string chord = by > 1 ? std::string("shift+") + key : key;
						p.on_key(chord, [&p, w, spec, layers, group = holds_others(*chosen), id = picked, b = box_of(*chosen), dx, dy, by] {
							model::box to{b.x + dx * by, b.y + dy * by, b.w, b.h};
							w->going = std::pair{id, to};
							if (group) {
								carry_from(*w, layers, id, b);
								return place_carried(p, *w, spec, to);
							}
							p.run(spec.place, collage_fields_of(value(fields{{"id", id}, {"x", to.x}, {"y", to.y}, {"w", to.w}, {"h", to.h}}), spec.fields));
						});
					}
				}
			}
		}
		// Changed as a picture's made ready away from the window, so it's drawn again.
		auto woken = p.kept<int>("collage ready " + spec.key);
		(void)woken.get();
		auto paint = [&p, w, data, layers, width, height, picked, full, over, spec, woken](desktop::canvas& c, desktop::rect area) {
			const auto& t = current_theme();
			c.box(area, {.fill = full ? desktop::color::hex(0x000000) : t.sunken});
			if (width <= 0 || height <= 0) return;
			// Fitted in its area, then zoomed and moved as it's looked at; what's past its
			// area isn't drawn.
			float margin = full ? 0 : 32;
			w->area = area, w->margin = margin;
			desktop::rect board = as_rect(model::board_of(w->view, {area.x, area.y, area.width, area.height}, width, height, margin));
			if (board.width <= 0) return;
			float scale = board.width / static_cast<float>(width), bw = board.width, bh = board.height;
			auto in_area = c.clip(area);
			w->board = board;
			w->scale = scale;
			if (!full) {
				desktop::color shade = desktop::color::hex(0x000000);
				shade.alpha = 0.18f;
				c.shadow(board, 0, 14, shade, {0, 2});
			}
			if (spec.clear) paint_checkers(c, board);
			else c.box(board, {.fill = desktop::color::hex(0xffffff)});
			// Matched, its pictures' colors are measured first, so each is made ready once.
			bool measuring = false;
			auto shifts = posting() ? shifts_of(p, layers, spec.match_colors, measured_soon(woken), &measuring) : shifts_of(p, layers, spec.match_colors, measured_now);
			// Each picture as it's ready to be drawn; while one never has been, the
			// collage's last picture of itself, its preview, stands in for all of it, so
			// it shows at once, as it was, and then as it is.
			std::map<std::string, ready_layer> ready;
			bool waiting = false;
			for (const auto& layer : layers.as_array()) {
				std::string id = layer["id"].as_string();
				if (!is_picture(layer) || (w->crop && w->crop->id == id) || !p.file_here(layer["picture"].as_string())) continue;
				if (auto made = ready_of(p, layer, shifts, width, height, woken, measuring)) ready.emplace(id, std::move(*made));
				else waiting = true;
			}
			if (waiting) {
				auto file = spec.preview_into.empty() ? std::nullopt : p.file_here(data[spec.preview_into].as_string());
				if (desktop::picture last = file ? picture_kept(*file) : desktop::picture()) {
					c.picture(last, board, {0, 0, 1, 1});
					return;
				}
			}
			std::optional<desktop::rect> framed;
			float framed_radius = 0;  // a picture's frame follows its corners, round as they are
			bool framed_words = false, chosen_picture = false, framed_group = false;
			std::vector<desktop::point> nodes;  // a picked path's, drawn instead of its frame
			std::vector<std::pair<desktop::point, desktop::point>> levers;  // its chosen node's control points, from the node
			std::optional<desktop::point> chosen_node;
			// While something's moved, resized or cropped, its pictures alone, nothing worked
			// out from where they are drawn around them, so what's being done is plain to see.
			bool effects = w->now.what == gesture::none && !w->crop;
			{
				auto inside = c.clip(board);
				if (effects) {
					if (desktop::picture fill = harmonized(harmonized_of(layers, ready), width, height)) c.picture(fill, board, {0, 0, 1, 1});
				}
				for (const auto& layer : layers.as_array()) {
					std::string id = layer["id"].as_string();
					model::box b = where_is(layer, *w);
					// A group draws nothing of its own; picked, it's framed around all it holds.
					if (holds_others(layer)) {
						if (id == picked && !full) framed = as_rect(w->on_board(b)), framed_group = true;
						continue;
					}
					if (is_words(layer)) {
						// Being written: as typed so far, with its caret, all of it marked while
						// the first key would replace it. Resized: its letters as large.
						bool writing = w->editing && w->editing->id == id;
						std::string said = writing ? w->editing->words : layer["words"].as_string();
						double size = layer["size"].as_double();
						model::box kept = box_of(layer);
						if (!writing && kept.h > 0) size *= b.h / kept.h;
						auto [mw, mh] = measured(said, size, weight_of(layer), italic_of(layer));
						b.w = mw, b.h = mh;
						desktop::rect on = as_rect(w->on_board(b));
						if (writing && w->editing->fresh) {
							desktop::color marked = t.accent;
							marked.alpha = 0.35f;
							c.box(on, {.fill = marked, .radius = 2});
						}
						desktop::rect caret = paint_words(c, layer, said, size * scale, {on.x, on.y}, writing ? w->editing->caret : std::string::npos);
						if (writing && !w->editing->fresh) c.box(caret, {.fill = t.accent});
						if (id == picked && !full) framed = on, framed_words = true;
						continue;
					}
					desktop::rect on = as_rect(w->on_board(b));
					if (is_shape(layer)) {
						// Being reshaped: as its nodes are now.
						value shown = layer;
						if (w->reshaped && w->reshaped->first == id) {
							auto [points, now] = reshaped_as(w->reshaped->second);
							shown.fields["points"] = value(points);
							b = now, on = as_rect(w->on_board(b));
						}
						paint_shape(c, on, b, shown);
						if (id == picked && !full) {
							framed = on, chosen_picture = true;
							// A path picked shows its nodes, not a box.
							if (is_path(shown) && !spec.reshape.empty()) {
								std::string outline = outline_of(shown, b);
								auto all = model::path_nodes(outline);
								for (const auto& n : all) {
									model::box at = w->on_board({n.x, n.y, 0, 0});
									nodes.push_back({static_cast<float>(at.x), static_cast<float>(at.y)});
								}
								if (w->node && w->node->first == id && w->node->second < all.size()) {
									chosen_node = nodes[nodes.size() - all.size() + w->node->second];
									for (const auto& h : model::path_handles(outline, w->node->second)) {
										model::box at = w->on_board({h.at.x, h.at.y, 0, 0});
										levers.emplace_back(*chosen_node, desktop::point{static_cast<float>(at.x), static_cast<float>(at.y)});
									}
								}
							}
						}
						continue;
					}
					if (std::string svg = vector_here(p, layer); !svg.empty()) {
						paint_vector(c, on, svg, layer);
						if (id == picked && !full) framed = on, chosen_picture = true;
						continue;
					}
					if (w->crop && w->crop->id == id) {
						// Being cropped: the whole picture faint, what's kept as it will be.
						if (desktop::picture shown = layer_picture(p, layer)) {
							const auto& e = *w->crop;
							c.picture(shown, as_rect(w->on_board(e.whole)), {0, 0, 1, 1}, 0.35f);
							model::box from{(e.kept.x - e.whole.x) / e.whole.w, (e.kept.y - e.whole.y) / e.whole.h, e.kept.w / e.whole.w, e.kept.h / e.whole.h};
							on = as_rect(w->on_board(e.kept));
							c.picture(shown, on, as_rect(from));
						}
					} else if (auto made = ready.find(id); made != ready.end()) {
						paint_picture(c, board, on, layer, made->second.seen, made->second.halo, effects);
					}
					if (id == picked && !full) {
						framed = on, chosen_picture = true;
						if (!(w->crop && w->crop->id == id)) framed_radius = std::min(static_cast<float>(layer["corners"].as_double()) * std::min(on.width, on.height), std::min(on.width, on.height) / 2);
					}
				}
			}
			// The lines a moved layer snapped to, across the whole collage.
			if (w->now.what == gesture::moving || w->now.what == gesture::resizing) {
				if (w->corner) {
					auto [corner, own] = *w->corner;
					c.line({board.x + static_cast<float>(corner.x) * scale, board.y + static_cast<float>(corner.y) * scale},
					       {board.x + static_cast<float>(own.x) * scale, board.y + static_cast<float>(own.y) * scale}, 1.5f, t.accent);
				}
				if (w->guide_x) c.box({board.x + static_cast<float>(*w->guide_x) * scale - 0.5f, board.y, 1, bh}, {.fill = t.accent});
				if (w->guide_y) c.box({board.x, board.y + static_cast<float>(*w->guide_y) * scale - 0.5f, bw, 1}, {.fill = t.accent});
			}
			// Words being typed, not made yet, with their caret.
			if (w->editing && w->editing->unmade) {
				const value& made = *w->editing->unmade;
				desktop::rect on = as_rect(w->on_board({made["x"].as_double(), made["y"].as_double(), 0, 0}));
				desktop::rect caret = paint_words(c, made, w->editing->words, made["size"].as_double() * scale, {on.x, on.y}, w->editing->caret);
				c.box(caret, {.fill = t.accent});
			}
			// A layer being drawn, as big as the drag so far.
			if (w->drawing) {
				model::box d = w->on_board(*w->drawing);
				c.box({static_cast<float>(d.x), static_cast<float>(d.y), static_cast<float>(d.w), static_cast<float>(d.h)}, {.border = t.accent, .border_width = 1.5f});
			}
			// A pen's path so far, the next segment following the pointer, and its nodes.
			if (!w->pen.empty()) {
				auto shown = w->pen;
				if (w->hover && w->now.what != gesture::drawing) shown.push_back({w->hover->x, w->hover->y, 0, 0});
				if (shown.size() > 1) {
					auto [points, b] = reshaped_as(model::pen_path(shown, false));
					value drawn = value(fields{{"shape", "path"}, {"points", points}, {"stroke", "#2457d6"}, {"stroke_width", 1.5 / w->scale}});
					paint_shape(c, as_rect(w->on_board(b)), b, drawn);
				}
				for (const auto& n : w->pen) {
					model::box at = w->on_board({n.x, n.y, 0, 0});
					c.box({static_cast<float>(at.x) - handle / 2, static_cast<float>(at.y) - handle / 2, handle, handle}, {.fill = desktop::color::hex(0xffffff), .border = t.accent, .border_width = 1.5f});
				}
			}
			// What a drag by hand has gone through so far.
			for (std::size_t i = 1; i < w->hand.size() && w->now.what == gesture::drawing; ++i) {
				model::box a = w->on_board({w->hand[i - 1].x, w->hand[i - 1].y, 0, 0}), b = w->on_board({w->hand[i].x, w->hand[i].y, 0, 0});
				c.line({static_cast<float>(a.x), static_cast<float>(a.y)}, {static_cast<float>(b.x), static_cast<float>(b.y)}, 1.5f, t.accent);
			}
			if (w->line) {
				model::box d = w->on_board(w->line->at);
				double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
				std::sscanf(w->line->points.c_str(), "M %lf %lf L %lf %lf", &x0, &y0, &x1, &y1);
				c.line({static_cast<float>(d.x + x0 * d.w), static_cast<float>(d.y + y0 * d.h)}, {static_cast<float>(d.x + x1 * d.w), static_cast<float>(d.y + y1 * d.h)}, 1.5f, t.accent);
			}
			if (layers.as_array().empty() && !full && !spec.empty.empty() && !w->editing) {
				auto said = desktop::lay_out(desktop::text_span{spec.empty, t.quiet()}, bw - 32, c.scale());
				c.text(said, {board.x + (bw - said.width()) / 2, board.y + (bh - said.height()) / 2});
			}
			// A group gone into, faintly framed; several chosen, each framed finely and all of
			// them together, moving as one; and a drag around what's to be chosen.
			desktop::color faint = t.accent;
			faint.alpha = 0.45f;
			for (const auto& l : layers.as_array()) {
				if (!w->entered.empty() && l["id"].as_string() == w->entered) c.box(as_rect(w->on_board(where_is(l, *w))).inset(-4), {.border = faint, .border_width = 1, .radius = 2});
			}
			if (w->chosen.size() > 1) {
				bool moving = w->going && w->going->first == "chosen";
				if (!moving) {
					for (const auto& l : layers.as_array()) {
						if (std::ranges::contains(w->chosen, l["id"].as_string())) c.box(as_rect(w->on_board(where_is(l, *w))), {.border = faint, .border_width = 1});
					}
				}
				model::box all_of = moving ? w->going->second : chosen_box(*w, layers);
				c.box(as_rect(w->on_board(all_of)).inset(-3), {.border = t.accent, .border_width = 1.5f, .radius = 2});
			}
			if (w->marquee) {
				desktop::color tint = t.accent;
				tint.alpha = 0.08f;
				c.box(as_rect(w->on_board(*w->marquee)), {.fill = tint, .border = t.accent, .border_width = 1});
			}
			// The picked layer's frame and corners, over everything, a crop's edges with
			// handles too, and its buttons just above it, or below when there's no room.
			w->tools.clear();
			if (!framed) return;
			if (nodes.empty()) c.box(*framed, {.border = t.accent, .border_width = 2, .radius = framed_radius});
			std::vector<desktop::point> handles = nodes;
			bool corners = nodes.empty() && (w->crop ? true : framed_words ? !spec.write.empty() : !spec.place.empty());
			if (corners && !w->editing) handles = {{framed->x, framed->y}, {framed->right(), framed->y}, {framed->right(), framed->bottom()}, {framed->x, framed->bottom()}};
			if (w->crop) {
				float mid_x = framed->x + framed->width / 2, mid_y = framed->y + framed->height / 2;
				handles.insert(handles.end(), {{mid_x, framed->y}, {framed->right(), mid_y}, {mid_x, framed->bottom()}, {framed->x, mid_y}});
			}
			for (const auto& [from, to] : levers) c.line(from, to, 1, t.accent);
			for (auto at : handles) {
				bool chosen = chosen_node && at.x == chosen_node->x && at.y == chosen_node->y;
				c.box({at.x - handle / 2, at.y - handle / 2, handle, handle}, {.fill = chosen ? t.accent : desktop::color::hex(0xffffff), .border = t.accent, .border_width = 2, .radius = 2});
			}
			// A control point is round, as a node is square.
			for (const auto& [_, at] : levers) {
				c.box({at.x - handle / 2 + 1, at.y - handle / 2 + 1, handle - 2, handle - 2}, {.fill = desktop::color::hex(0xffffff), .border = t.accent, .border_width = 1.5f, .radius = handle / 2});
			}
			// While cropping, keep and put away; otherwise the buttons the screen gives.
			std::vector<std::pair<std::string, std::string>> drawn;  // icon, label
			if (w->crop) drawn = {{"check", "Keep"}, {"close", "Put away"}};
			else {
				for (const auto& a : spec.asks) {
					bool fits = a.does == "crop" ? chosen_picture : a.does == "edit" ? framed_words : a.does == "ungroup" ? framed_group : true;
					if (fits) drawn.emplace_back(a.icon, a.label);
				}
				for (const auto& [b, _] : over) drawn.emplace_back(b.icon, b.label);
			}
			if (drawn.empty()) return;
			constexpr float button = 30, gap = 2, pad = 4, apart = 12;
			float tools_w = static_cast<float>(drawn.size()) * button + static_cast<float>(drawn.size() - 1) * gap + 2 * pad, tools_h = button + 2 * pad;
			float x = std::clamp(framed->x + (framed->width - tools_w) / 2, area.x + 4, std::max(area.x + 4, area.right() - tools_w - 4));
			float y = framed->y - apart - tools_h;
			if (y < area.y + 4) y = std::min(framed->bottom() + apart, area.bottom() - tools_h - 4);
			desktop::rect bar{x, y, tools_w, tools_h};
			desktop::color shade = desktop::color::hex(0x000000);
			shade.alpha = 0.22f;
			c.shadow(bar, 8, 12, shade, {0, 2});
			c.box(bar, {.fill = t.surface, .border = t.line, .border_width = 1, .radius = 8});
			for (std::size_t i = 0; i < drawn.size(); ++i) {
				desktop::rect at{x + pad + static_cast<float>(i) * (button + gap), y + pad, button, button};
				const auto& [icon, label] = drawn[i];
				bool keep = icon == "check", away = icon == "delete";
				if (keep) c.box(at, {.fill = t.accent, .radius = 6});
				if (desktop::has_icon(icon)) desktop::draw_icon(c, icon, {at.x + 7, at.y + 7, 16, 16}, keep ? desktop::color::hex(0xffffff) : away ? t.danger : t.ink);
				else c.text(desktop::lay_out(desktop::text_span{label.substr(0, 1), t.strong()}, button, c.scale()), {at.x + 10, at.y + 6});
				w->tools.emplace_back(at, i);
			}
		};
		desktop::drawing_pointer pointer{
		    .on_press =
		        [&p, w, data, layers, width, height, picked, over, spec](desktop::point at, int clicks) {
			        // Filling the screen, the collage is only looked at: a click goes back.
			        if (fullscreen()) {
				        fullscreen(false);
				        return true;
			        }
			        if (w->scale <= 0) return false;
			        // The buttons over the picked layer first: while cropping, keep or put away.
			        for (const auto& [area, i] : w->tools) {
				        if (!area.contains(at)) continue;
				        if (w->crop) {
					        if (i == 0) keep_crop(p, *w, spec);
					        else w->crop.reset();
					        return true;
				        }
				        // The asks that fit what's picked first, then the commands.
				        std::vector<std::string> fitting;
				        for (const auto& l : layers.as_array()) {
					        if (l["id"].as_string() != picked) continue;
					        for (const auto& a : spec.asks) {
						        bool fits = a.does == "crop" ? is_picture(l) && vector_here(p, l).empty() : a.does == "edit" ? is_words(l) : a.does == "ungroup" ? holds_others(l) : true;
						        if (fits) fitting.push_back(a.does);
					        }
				        }
				        if (i < fitting.size()) p.kept<std::string>("collage do " + spec.key).set(fitting[i]);
				        else run_over(p, spec, *w, layers, over[i - fitting.size()].first.command, over[i - fitting.size()].second.fields);
				        return true;
			        }
			        model::point on{at.x, at.y};
			        // Words being written are kept by a press anywhere but on them.
			        if (w->editing) {
				        bool on_them = false;
				        if (const auto& made = w->editing->unmade) {
					        auto [mw, mh] = measured(w->editing->words, (*made)["size"].as_double(), weight_of(*made), italic_of(*made));
					        on_them = w->on_board({(*made)["x"].as_double(), (*made)["y"].as_double(), mw, mh}).contains(on);
				        }
				        for (const auto& l : layers.as_array()) {
					        if (l["id"].as_string() != w->editing->id) continue;
					        auto [mw, mh] = measured(w->editing->words, l["size"].as_double(), weight_of(l), italic_of(l));
					        model::box b = box_of(l);
					        b.w = mw, b.h = mh;
					        on_them = w->on_board(b).contains(on);
				        }
				        if (on_them) return true;
				        finish_writing(p, *w, layers, spec);
			        }
			        // Cropping: its edges and corners trim it, inside moves it, the rest of the
			        // picture shifts with it, and a press anywhere else keeps it.
			        if (w->crop) {
				        int edges = model::crop_edges_at(w->on_board(w->crop->kept), on, handle);
				        if (edges < 0 && w->on_board(w->crop->whole).contains(on)) {
					        w->now = {.what = gesture::shifting, .id = w->crop->id, .from = w->crop->whole, .at = at, .corner = 0, .edges = 0, .kept_from = w->crop->kept};
					        return true;
				        }
				        if (edges < 0) {
					        keep_crop(p, *w, spec);
					        return true;
				        }
				        w->now = {.what = gesture::cropping, .id = w->crop->id, .from = w->crop->kept, .at = at, .corner = 0, .edges = edges, .kept_from = {}};
				        return true;
			        }
			        // While something's to be drawn, a press on the collage starts drawing it,
			        // over whatever's there.
			        // A pen puts down a node: a double-click ends its path, and a press on its
			        // first node closes it.
			        if (const collage_draw* d = drawing_now(spec); d && d->by == "points" && w->board.contains(at)) {
				        if (clicks >= 2 && !w->pen.empty()) {
					        pen_done(p, spec, layers, *w, false);
					        return true;
				        }
				        if (w->pen.size() > 2) {
					        model::box first = w->on_board({w->pen.front().x, w->pen.front().y, 0, 0});
					        if (std::abs(first.x - at.x) <= handle && std::abs(first.y - at.y) <= handle) {
						        pen_done(p, spec, layers, *w, true);
						        return true;
					        }
				        }
				        model::point on = w->on_collage(at);
				        w->pen.push_back({on.x, on.y, 0, 0});
				        w->now = {.what = gesture::drawing, .id = "points", .from = {}, .at = at, .corner = 0, .edges = 0, .kept_from = {}};
				        return true;
			        }
			        // Typing: words begun where it's pressed, nothing picked meanwhile.
			        if (const collage_draw* d = drawing_now(spec); d && d->by == "typing" && w->board.contains(at) && !desktop::held().space) {
				        w->editing = typing_at(p, spec, *d, w->on_collage(at), height);
				        if (!spec.pick.empty()) p.pick(spec.pick, "");
				        return true;
			        }
			        // With Space held, a drag moves what's shown, as a drawing program's hand does.
			        if (desktop::held().space) {
				        w->now = {.what = gesture::panning, .id = "", .from = {w->view.pan_x, w->view.pan_y, 0, 0}, .at = at, .corner = 0, .edges = 0, .kept_from = {}};
				        return true;
			        }
			        if (const collage_draw* d = drawing_now(spec); d && w->board.contains(at)) {
				        // Its id says how it's drawn: a box, or by line.
				        w->now = {.what = gesture::drawing, .id = d->by, .from = {}, .at = at, .corner = 0, .edges = 0, .kept_from = {}};
				        w->hand = {w->on_collage(at)};
				        return true;
			        }
			        // A node of the picked path reshapes it, and shows its control points,
			        // which reshape it too.
			        for (const auto& l : layers.as_array()) {
				        if (l["id"].as_string() != picked || !is_path(l) || holds_others(l) || spec.reshape.empty()) continue;
				        std::string outline = outline_of(l, box_of(l));
				        if (w->node && w->node->first == picked) {
					        for (const auto& h : model::path_handles(outline, w->node->second)) {
						        model::box lever = w->on_board({h.at.x, h.at.y, 0, 0});
						        if (std::abs(lever.x - at.x) > handle || std::abs(lever.y - at.y) > handle) continue;
						        w->now = {.what = gesture::reshaping, .id = picked, .from = box_of(l), .at = at, .corner = static_cast<int>(w->node->second), .edges = h.out ? 2 : 1, .kept_from = {}};
						        w->reshape_from = outline;
						        return true;
					        }
				        }
				        auto nodes = model::path_nodes(outline);
				        for (std::size_t k = 0; k < nodes.size(); ++k) {
					        model::box node = w->on_board({nodes[k].x, nodes[k].y, 0, 0});
					        if (std::abs(node.x - at.x) > handle || std::abs(node.y - at.y) > handle) continue;
					        w->now = {.what = gesture::reshaping, .id = picked, .from = box_of(l), .at = at, .corner = static_cast<int>(k), .edges = 0, .kept_from = {}};
					        w->reshape_from = outline;
					        w->node = std::pair{picked, k};
					        return true;
				        }
			        }
			        // A corner of the picked layer resizes it: a picture with its shape kept,
			        // words by their size. A path has nodes, not corners.
			        for (const auto& l : layers.as_array()) {
				        if (l["id"].as_string() != picked || (is_path(l) && !holds_others(l))) continue;
				        if (is_words(l) ? spec.write.empty() : spec.place.empty()) continue;
				        model::box b = where_is(l, *w);
				        if (is_words(l)) {
					        auto [mw, mh] = measured(l["words"].as_string(), l["size"].as_double(), weight_of(l), italic_of(l));
					        b.w = mw, b.h = mh;
				        }
				        if (int corner = model::corner_at(w->on_board(b), on, handle); corner >= 0) {
					        // A group keeps its shape unless Shift, as a picture does.
					        w->now = {.what = gesture::resizing, .id = picked, .from = b, .at = at, .corner = corner, .edges = 0, .kept_from = {}, .free = is_shape(l) && !holds_others(l)};
					        if (holds_others(l)) carry_from(*w, layers, picked, b);
					        snap_lines(layers, width, height, *w, picked);
					        return true;
				        }
			        }
			        // The top layer under the pointer is picked and moves; pressed twice, a picture
			        // is cropped and words written.
			        const auto& all = layers.as_array();
			        bool adding = desktop::held().shift;
			        for (std::size_t i = all.size(); i-- > 0;) {
				        if (holds_others(all[i])) continue;
				        model::box b = where_is(all[i], *w);
				        if (is_words(all[i])) {
					        auto [mw, mh] = measured(all[i]["words"].as_string(), all[i]["size"].as_double(), weight_of(all[i]), italic_of(all[i]));
					        b.w = mw, b.h = mh;
				        }
				        if (!hits(all[i], b, w->on_collage(at), 1 / w->scale)) continue;
				        std::string hit = all[i]["id"].as_string();
				        w->node.reset();
				        // One a group holds is picked as the group, the outermost, or the one
				        // just inside the group gone into; a press outside that leaves it. A
				        // group pressed twice is gone into, as a drawing program does.
				        if (!w->entered.empty() && !w->inside(hit, w->entered)) w->entered.clear();
				        std::string id = w->below(hit, w->entered);
				        if (clicks >= 2 && id != hit) {
					        w->entered = id;
					        id = w->below(hit, id);
				        }
				        const value* row = &all[i];
				        for (const auto& l : all) {
					        if (l["id"].as_string() == id) row = &l;
				        }
				        if (row != &all[i]) b = where_is(*row, *w);
				        // With Shift, it's chosen or let go, beside what's chosen already.
				        if (adding) {
					        if (w->chosen.empty() && !picked.empty()) w->chosen.push_back(picked);
					        if (auto found = std::ranges::find(w->chosen, id); found != w->chosen.end()) w->chosen.erase(found);
					        else w->chosen.push_back(id);
					        pick_chosen(p, spec, *w);
					        return true;
				        }
				        // One of several chosen moves them all.
				        if (w->chosen.size() > 1 && std::ranges::contains(w->chosen, id)) {
					        if (spec.place.empty()) return true;
					        model::box all_of = chosen_box(*w, layers);
					        w->now = {.what = gesture::moving, .id = "chosen", .from = all_of, .at = at, .corner = 0, .edges = 0, .kept_from = {}};
					        carry_from(*w, layers, "chosen", all_of);
					        snap_lines(layers, width, height, *w, "chosen");
					        return true;
				        }
				        w->chosen.clear();
				        if (!spec.pick.empty()) p.pick(spec.pick, id);
				        // Its layer is the current one, as new objects go in it.
				        if (!spec.layer_pick.empty() && !all[i][spec.layer].as_string().empty()) p.pick(spec.layer_pick, all[i][spec.layer].as_string());
				        if (clicks >= 2 && id == hit) {
					        if (is_words(all[i]) && !spec.write.empty()) w->editing = placed::writing{id, all[i]["words"].as_string(), false};
					        else if (is_picture(all[i]) && vector_here(p, all[i]).empty() && !spec.crop.empty()) w->crop = crop_begun(all[i]);
					        return true;
				        }
				        if (!spec.place.empty()) {
					        w->now = {.what = gesture::moving, .id = id, .from = b, .at = at, .corner = 0, .edges = 0, .kept_from = {}};
					        if (holds_others(*row)) carry_from(*w, layers, id, b);
					        snap_lines(layers, width, height, *w, id);
				        }
				        return true;
			        }
			        // On nothing, or off the collage, nothing is picked, and a group gone into
			        // is left; a drag from there picks what it goes all the way around.
			        w->node.reset();
			        w->entered.clear();
			        if (!adding) {
				        w->chosen.clear();
				        if (!spec.pick.empty()) p.pick(spec.pick, "");
			        }
			        if (spec.pick.empty() || !w->board.contains(at)) return false;
			        model::point from = w->on_collage(at);
			        w->now = {.what = gesture::selecting, .id = "", .from = {from.x, from.y, 0, 0}, .at = at, .corner = 0, .edges = 0, .kept_from = {}};
			        w->marquee = model::box{from.x, from.y, 0, 0};
			        return true;
		        },
		    .on_drag =
		        [w, width, height, snap = spec.snap](desktop::point at) {
			        if (w->now.what == gesture::none) return;
			        if (w->now.what == gesture::panning) {
				        w->view.pan_x = w->now.from.x + (at.x - w->now.at.x), w->view.pan_y = w->now.from.y + (at.y - w->now.at.y);
				        return;
			        }
			        if (w->now.what == gesture::selecting) {
				        model::point a{w->now.from.x, w->now.from.y}, z = w->on_collage(at);
				        w->marquee = model::box{std::min(a.x, z.x), std::min(a.y, z.y), std::abs(z.x - a.x), std::abs(z.y - a.y)};
				        return;
			        }
			        if (w->now.what == gesture::reshaping) {
				        double dx = (at.x - w->now.at.x) / w->scale, dy = (at.y - w->now.at.y) / w->scale;
				        w->reshaped = std::pair{w->now.id, reshaped_by(w->reshape_from, w->now, dx, dy)};
				        return;
			        }
			        if (w->now.what == gesture::drawing) {
				        // A pen's node dragged from: its handle, the way the curve leaves it.
				        if (w->now.id == "points") {
					        if (w->pen.empty()) return;
					        model::point on = w->on_collage(at);
					        w->pen.back().handle_x = on.x - w->pen.back().x, w->pen.back().handle_y = on.y - w->pen.back().y;
					        w->hover = on;
					        return;
				        }
				        if (w->now.id == "hand") {
					        w->hand.push_back(w->on_collage(at));
					        return;
				        }
				        bool line = w->now.id == "line";
				        if (line) w->line = model::drawn_line(w->on_collage(w->now.at), w->on_collage(at), desktop::held().shift, 4 / w->scale, width, height);
				        else w->drawing = model::drawn(w->on_collage(w->now.at), w->on_collage(at), desktop::held().shift, 4 / w->scale, width, height);
				        return;
			        }
			        if (w->now.what == gesture::shifting) return shift_crop(*w, at);
			        if (w->now.what == gesture::cropping) {
				        if (w->crop) w->crop->kept = going_to(*w, at, width, height, false);
				        return;
			        }
			        w->going = std::pair{w->now.id, going_to(*w, at, width, height, snap)};
		        },
		    .on_release =
		        [&p, w, width, height, layers, spec](desktop::point at) {
			        if (w->now.what == gesture::none) return;
			        if (w->now.what == gesture::panning) return void(w->now = {});
			        // What the drag went all the way around is chosen, as the groups they're in
			        // are, or what's just inside the group gone into.
			        if (w->now.what == gesture::selecting) {
				        model::point a{w->now.from.x, w->now.from.y}, z = w->on_collage(at);
				        w->marquee = model::box{std::min(a.x, z.x), std::min(a.y, z.y), std::abs(z.x - a.x), std::abs(z.y - a.y)};
				        w->now = {};
				        auto around = std::exchange(w->marquee, std::nullopt);
				        if (!around || (around->w * w->scale < 3 && around->h * w->scale < 3)) return;
				        for (const auto& l : layers.as_array()) {
					        std::string id = l["id"].as_string();
					        if (l["group"].as_string() != w->entered) continue;
					        model::box b = where_is(l, *w);
					        bool inside = b.x >= around->x && b.y >= around->y && b.x + b.w <= around->x + around->w && b.y + b.h <= around->y + around->h;
					        if (inside && !std::ranges::contains(w->chosen, id)) w->chosen.push_back(id);
				        }
				        pick_chosen(p, spec, *w);
				        return;
			        }
			        if (w->now.what == gesture::reshaping) {
				        gesture g = std::exchange(w->now, {});
				        double dx = (at.x - g.at.x) / w->scale, dy = (at.y - g.at.y) / w->scale;
				        if (dx == 0 && dy == 0) return w->reshaped.reset();
				        std::string outline = reshaped_by(w->reshape_from, g, dx, dy);
				        w->reshaped = std::pair{g.id, outline};
				        auto [points, b] = reshaped_as(outline);
				        // Kept, it's drawn from the view again; turned down, as it was.
				        p.run(spec.reshape, collage_fields_of(value(fields{{"id", g.id}, {"points", points}, {"x", b.x}, {"y", b.y}, {"w", b.w}, {"h", b.h}}), spec.fields),
				              [w](const answer&) { w->reshaped.reset(); });
				        return;
			        }
			        if (w->now.what == gesture::drawing) {
				        gesture g = std::exchange(w->now, {});
				        w->drawing.reset(), w->line.reset();
				        // A pen's node is put down; its path goes on until it's ended.
				        if (g.id == "points") return;
				        if (g.id == "hand") {
					        // What it went through, only as much as keeps its shape, as one curve.
					        auto hand = std::exchange(w->hand, {});
					        hand.push_back(w->on_collage(at));
					        std::string outline = model::freehand(hand, 4 / w->scale);
					        if (outline.empty()) return;
					        auto [points, b] = reshaped_as(outline);
					        draw_layer(p, spec, layers, b, points);
					        return;
				        }
				        if (g.id == "line") {
					        auto line = model::drawn_line(w->on_collage(g.at), w->on_collage(at), desktop::held().shift, 4 / w->scale, width, height);
					        draw_layer(p, spec, layers, line.at, line.points);
				        } else {
					        draw_layer(p, spec, layers, model::drawn(w->on_collage(g.at), w->on_collage(at), desktop::held().shift, 4 / w->scale, width, height));
				        }
				        return;
			        }
			        if (w->now.what == gesture::shifting || w->now.what == gesture::cropping) {
				        if (w->now.what == gesture::shifting) shift_crop(*w, at);
				        else if (w->crop) w->crop->kept = going_to(*w, at, width, height, false);
				        w->now = {};
				        return;
			        }
			        model::box to = going_to(*w, at, width, height, spec.snap);
			        w->guide_x.reset(), w->guide_y.reset(), w->corner.reset();
			        gesture g = std::exchange(w->now, {});
			        if (to.x == g.from.x && to.y == g.from.y && to.w == g.from.w && to.h == g.from.h) {
				        w->going.reset();
				        return;
			        }
			        w->going = std::pair{g.id, to};
			        // Where it's taken, kept; turned down, it goes back where it was.
			        auto back = [w](const answer& a) {
				        if (!a) w->going.reset(), w->carried_from.clear();
			        };
			        if (!w->carried_from.empty() && w->carrying == g.id) return place_carried(p, *w, spec, to, back);
			        // Words resized are larger or smaller words, not stretched ones.
			        for (const auto& l : layers.as_array()) {
				        if (l["id"].as_string() == g.id && is_words(l) && g.what == gesture::resizing && g.from.h > 0) {
					        double size = l["size"].as_double() * to.h / g.from.h;
					        auto [mw, mh] = measured(l["words"].as_string(), size, weight_of(l), italic_of(l));
					        p.run(spec.write, collage_fields_of(value(fields{{"id", g.id}, {"words", l["words"]}, {"size", size}, {"x", to.x}, {"y", to.y}, {"w", mw}, {"h", mh}}), spec.fields), back);
					        return;
				        }
			        }
			        p.run(spec.place, collage_fields_of(value(fields{{"id", g.id}, {"x", to.x}, {"y", to.y}, {"w", to.w}, {"h", to.h}}), spec.fields), back);
		        },
		    // The wheel, with Ctrl, zooms about the pointer, as a trackpad's pinch does; without,
		    // it moves what's shown, across with Shift.
		    .on_wheel =
		        [w, width, height](desktop::point at, float dx, float dy) {
			        if (width <= 0 || height <= 0) return false;
			        desktop::rect a = w->area;
			        if (desktop::held().control) {
				        w->view = model::zoomed(w->view, {a.x, a.y, a.width, a.height}, width, height, w->margin, std::exp(-dy * 0.01), {at.x, at.y});
				        return true;
			        }
			        if (desktop::held().shift) std::swap(dx, dy);
			        w->view.pan_x -= dx, w->view.pan_y -= dy;
			        return true;
		        },
		    .cursor =
		        [&p, w, data, layers, width, height, picked, spec](desktop::point at) {
			        if (w->scale <= 0) return desktop::cursor::normal;
			        for (const auto& [area, i] : w->tools) {
				        if (area.contains(at)) return desktop::cursor::pointer;
			        }
			        model::point on{at.x, at.y};
			        if (w->crop) {
				        int edges = model::crop_edges_at(w->on_board(w->crop->kept), on, handle);
				        if (edges < 0) return w->on_board(w->crop->whole).contains(on) ? desktop::cursor::grab : desktop::cursor::normal;
				        if (edges == 0) return desktop::cursor::move;
				        if (edges == 3 || edges == 12) return desktop::cursor::resize_falling;
				        if (edges == 6 || edges == 9) return desktop::cursor::resize_rising;
				        return edges & 5 ? desktop::cursor::resize_across : desktop::cursor::resize_up_down;
			        }
			        if (w->editing) return desktop::cursor::text;
			        if (desktop::held().space || w->now.what == gesture::panning) return w->now.what == gesture::panning ? desktop::cursor::grabbing : desktop::cursor::grab;
			        // The next segment of a pen's path follows the pointer.
			        if (!w->pen.empty()) w->hover = w->on_collage(at);
			        if (const collage_draw* d = drawing_now(spec); d && w->board.contains(at)) return d->by == "typing" ? desktop::cursor::text : desktop::cursor::crosshair;
			        for (const auto& l : layers.as_array()) {
				        if (l["id"].as_string() != picked || is_words(l) || is_path(l) || spec.place.empty()) continue;
				        int corner = model::corner_at(w->on_board(where_is(l, *w)), on, handle);
				        if (corner == 0 || corner == 2) return desktop::cursor::resize_falling;
				        if (corner == 1 || corner == 3) return desktop::cursor::resize_rising;
			        }
			        const auto& all = layers.as_array();
			        for (std::size_t i = all.size(); i-- > 0;) {
				        if (hits(all[i], where_is(all[i], *w), w->on_collage(at), 1 / w->scale)) return spec.place.empty() ? desktop::cursor::pointer : desktop::cursor::move;
			        }
			        return desktop::cursor::normal;
		        },
		};
		desktop::view drawn = desktop::drawing(std::move(paint), std::move(pointer));
		// Files dropped on it, or a browser's picture, are pictures where they're let go.
		if (spec.paste) {
			drawn = desktop::file_target(
			    std::move(drawn),
			    [&p, w, spec](desktop::dropped what, desktop::point at) {
				    std::optional<model::point> on;
				    if (w->scale > 0 && w->board.contains(at)) on = w->on_collage(at);
				    if (!what.picture.empty()) what.files.push_back(kept_for_now(what.picture));
				    if (what.files.empty() && !what.elsewhere.empty()) {
					    p.told().set("That picture is on the web. Save it first, then drop it here.");
					    return;
				    }
				    take_pictures(p, spec, what.files, on);
			    },
			    {.border = current_theme().accent, .border_width = 3});
		}
		if (!spec.fills) return std::move(drawn).height(560);
		return std::move(drawn).grow();
	}

	namespace {
		// A list's preview of some of a collage's objects, as wide as this, as tall as the
		// collage's proportions make it: them alone, drawn where they are, faint while
		// hidden, made once for each look of them.
		constexpr float preview_wide = 40;
		desktop::view preview_of(page& p, const value& objects, double width, double height, bool clear, bool hidden) {
			float tall = width > 0 && height > 0 ? std::clamp(static_cast<float>(preview_wide * height / width), 12.f, preview_wide) : preview_wide * 9 / 16;
			desktop::picture drawn;
			if (width > 0 && height > 0 && !objects.as_array().empty()) {
				static kept_by<desktop::picture> made(256);
				std::vector<value> shown;
				for (const auto& o : objects.as_array()) {
					value one = o;
					one.fields.erase("hidden");
					shown.push_back(std::move(one));
				}
				value all = value(std::move(shown));
				drawn = made.get(std::format("{} {} {} {}", ember::to_json(all), width, height, preview_wide), [&] {
					auto alone = gathered(p, all, width, height, 2 * preview_wide / width, false);
					alone.clear = true;
					return drawn_whole(alone);
				});
			}
			return desktop::drawing([drawn, clear, hidden](desktop::canvas& c, desktop::rect area) {
				       const auto& th = current_theme();
				       if (clear) paint_checkers(c, area);
				       else c.box(area, {.fill = desktop::color::hex(0xffffff)});
				       if (drawn) c.picture(drawn, area, {0, 0, 1, 1}, hidden ? 0.35f : 1.f);
				       c.box(area, {.border = th.line, .border_width = 1});
			       })
			    .width(preview_wide)
			    .height(tall)
			    .grow(0);
		}

		// How a name in a list of layers is set: small and light, so the list stays quiet; a
		// layer's in ink, an object's smaller and gray, and a hidden one's gray too.
		desktop::text_style name_style(bool hidden, bool object) {
			const auto& t = current_theme();
			desktop::text_style style = t.quiet();
			style.weight = 400;
			style.size -= object ? 2 : 1;
			if (!hidden && !object) style.color = t.ink;
			return style;
		}

		// A name in a list of layers, cut off at its room: a press picks its row, and a
		// double-click, given a command to send it to, writes it in place, Enter keeping it.
		desktop::view name_of_row(page& p, const std::string& id, std::string name, desktop::text_style style, const std::string& rename, std::function<void()> pick,
		                          const std::map<std::string, std::string>& fields = {}) {
			auto renaming = p.kept<std::string>("layers renaming");
			if (!rename.empty() && renaming.get() == id) {
				auto written = p.kept<std::string>("layers name " + id);
				return desktop::field(written, {.on_submit = [&p, rename, id, written, renaming, fields] {
					        p.run(rename, collage_fields_of(value(std::map<std::string, value>{{"id", id}, {"name", written.peek()}}), fields));
					        renaming.set("");
				        }})
				    .autofocus()
				    .width(0)
				    .grow();
			}
			auto started = p.kept<std::string>("layers name " + id);
			float tall = style.size * 1.4f;
			return desktop::drawing([name, style](desktop::canvas& c, desktop::rect area) {
				       auto kept = c.clip(area);
				       auto laid = desktop::lay_out(desktop::text_span{name, style}, 100000, c.scale());
				       c.text(laid, {area.x, area.y + (area.height - laid.height()) / 2});
			       },
			                        {.on_press = [pick, rename, renaming, started, id, name](desktop::point, int clicks) {
				                         // A double-click writes it; a single press is its row's, to pick or drag.
				                         if (clicks < 2 || rename.empty()) return false;
				                         started.set(name);
				                         renaming.set(id);
				                         pick();
				                         return true;
			                         }})
			    .height(tall)
			    .width(0)
			    .grow();
		}

		// An object's name in a list: its words' first line, or what it is.
		std::string object_name(page& p, const value& l) {
			std::string name = l["words"].as_string().substr(0, l["words"].as_string().find('\n'));
			if (!l["name"].as_string().empty()) return l["name"].as_string();
			if (holds_others(l)) return "Group";
			if (!is_words(l)) {
				// A path of two points is a line.
				std::string shape = l["shape"].as_string(), points = l["points"].as_string();
				bool line = std::ranges::count(points, 'L') == 1 && points.find('C') == std::string::npos;
				name = is_picture(l) ? (vector_here(p, l).empty() ? "Picture" : "SVG") : shape == "ellipse" ? "Ellipse" : shape == "path" ? (line ? "Line" : "Path") : "Rectangle";
			}
			if (name.size() > 28) name = name.substr(0, 27) + "…";
			return name;
		}

		// Renumbered by their place after the one at `from` moves to `to`, in one step.
		void moved(page& p, const std::string& command, const value& rows, std::size_t from, std::size_t to, const std::map<std::string, std::string>& fields) {
			bool first = true;
			for (const auto& [id, depth] : depths_moved(rows, from, to)) {
				value sent = collage_fields_of(value(std::map<std::string, value>{{"id", id}, {"depth", depth}}), fields);
				if (!std::exchange(first, false)) sent.fields["one:history"] = value("joined");
				p.run(command, sent);
			}
		}

		// A row of a list of layers: its eye in a column of its own, a thin line, then the
		// row itself, tinted while it's picked.
		desktop::view list_row(desktop::view eye, std::vector<desktop::view> row, bool picked, float indent, std::function<void()> pick) {
			const auto& t = current_theme();
			desktop::view inside = desktop::row(std::move(row))
			                           .spacing(4)
			                           .align(align::center)
			                           .padding(edges(3, 4, 3, 4 + indent))
			                           .background({.fill = picked ? t.sunken : t.surface, .border = picked ? t.accent : t.surface, .border_width = 1, .radius = t.corners})
			                           .on_click(std::move(pick))
			                           .grow();
			desktop::view column = eye.get() ? std::move(eye) : desktop::spacer().grow(0).width(20);
			return desktop::row(std::move(column).width(20), desktop::spacer().grow(0).width(1).height(20).background({.fill = t.line}), std::move(inside)).spacing(4).align(align::center);
		}

		// A list of layers' header, thin: its name, and a + that makes a new layer on top,
		// Layer and the next number, when it can.
		desktop::view layers_header(page& p, const layers_spec& spec, const std::vector<value>& groups) {
			const auto& t = current_theme();
			desktop::text_style said = t.quiet();
			said.weight = 600;
			std::vector<desktop::view> header;
			header.push_back(desktop::text("Layers", said));
			header.push_back(desktop::spacer());
			if (!spec.create.empty()) {
				double top = 0;
				for (const auto& g : groups) top = std::max(top, g["depth"].as_double());
				std::string name = "Layer " + std::to_string(groups.size() + 1);
				header.push_back(desktop::tool_button("add", "New layer", [&p, spec, name, top] {
					value made = value(std::map<std::string, value>{{"name", name}, {"depth", top + 1}});
					if (!spec.create_parent.empty()) made.fields[spec.create_parent] = value(spec.subject);
					p.run(spec.create, made, [&p, pick = spec.pick](const answer& a) {
						if (a && !pick.empty()) p.pick(pick, *a);
					});
				}, {.tiny = true}));
			}
			return desktop::row(std::move(header)).align(align::center).padding(edges(0, 4, 2, 4));
		}

		// The layers a collage's objects are held in, topmost first: each its eye, a
		// preview of its objects, its name, up, down and delete, and, opened, its
		// objects under it, topmost first, each its preview and name, and delete.
		desktop::view held_layers(page& p, const layers_spec& spec, const value& data, double width, double height) {
			const auto& t = current_theme();
			std::vector<value> groups = data[spec.list].as_array();
			std::stable_sort(groups.begin(), groups.end(), [](const value& a, const value& b) { return a["depth"].as_double() < b["depth"].as_double(); });
			const value ordered = value(groups);
			const auto& objects = data[spec.objects].as_array();
			if (groups.empty()) return desktop::text("No layers yet.", t.quiet());
			std::string picked = spec.pick.empty() ? "" : p.picked_id(spec.pick);
			std::string picked_object = spec.object_pick.empty() ? "" : p.picked_id(spec.object_pick);
			std::vector<desktop::view> rows;
			std::vector<bool> movable;
			std::vector<std::size_t> layer_of;  // each row's layer, by its place lowest first
			for (std::size_t i = groups.size(); i-- > 0;) {
				const value& g = groups[i];
				std::string id = g["id"].as_string();
				bool hidden = hidden_layer(g);
				std::vector<value> held;
				for (const auto& o : objects) {
					if (o[spec.object_layer].as_string() == id && !hidden_layer(o)) held.push_back(o);
				}
				held = grouped(value(held)).as_array();
				auto open = p.kept<bool>("layers open " + id);
				std::vector<desktop::view> row;
				desktop::view eye;
				// Opened or closed by a small chevron, so its name keeps its room.
				bool opened = open.get();
				row.push_back(desktop::drawing([opened](desktop::canvas& c, desktop::rect area) {
					              desktop::draw_icon(c, opened ? "chevron_down" : "chevron_right", {area.x, area.y + (area.height - 16) / 2, 16, 16}, current_theme().ink);
				              })
				                  .width(16)
				                  .height(28)
				                  .grow(0)
				                  .on_click([open] { open.set(!open.peek()); }));
				if (!spec.hide.empty()) {
					eye = desktop::tool_button(hidden ? "eye_off" : "eye", hidden ? "Show" : "Hide", [&p, command = spec.hide, id, hidden] {
						p.run(command, value(std::map<std::string, value>{{"id", id}, {"hidden", hidden ? 0 : 1}}));
					}, {.tiny = true});
				}
				row.push_back(preview_of(p, value(held), width, height, spec.clear, hidden));
				std::string name = g["name"].as_string().empty() ? "Layer" : g["name"].as_string();
				auto pick_layer = [&p, pick = spec.pick, id] {
					if (!pick.empty()) p.pick(pick, id);
				};
				row.push_back(name_of_row(p, id, std::move(name), name_style(hidden, false), spec.rename, pick_layer));
				if (!spec.remove.empty()) {
					// Its objects go with it, in one step.
					row.push_back(desktop::tool_button("delete", "Delete", [&p, spec, id, held] {
						bool first = true;
						for (const auto& o : held) {
							value sent = value(std::map<std::string, value>{{"id", o["id"]}});
							if (!std::exchange(first, false)) sent.fields["one:history"] = value("joined");
							if (!spec.object_remove.empty()) p.run(spec.object_remove, sent);
						}
						value gone = value(std::map<std::string, value>{{"id", id}});
						if (!first) gone.fields["one:history"] = value("joined");
						p.run(spec.remove, gone);
					}, {.tiny = true, .on_hover = true}));
				}
				rows.push_back(list_row(std::move(eye), std::move(row), id == picked, 0, [&p, pick = spec.pick, id] {
					if (!pick.empty()) p.pick(pick, id);
				}));
				movable.push_back(!spec.order.empty()), layer_of.push_back(i);
				if (!open.get()) continue;
				// Its objects, topmost first, a group's opened under it, each further in.
				std::function<void(const std::string&, float, int)> add = [&](const std::string& within, float indent, int depth) {
					for (std::size_t j = held.size(); j-- > 0;) {
						const value& o = held[j];
						if (o["group"].as_string() != within || depth > 32) continue;
						std::string object = o["id"].as_string();
						bool group = holds_others(o);
						auto shown = p.kept<bool>("layers open " + object);
						std::vector<desktop::view> inside;
						if (group) {
							bool opened_group = shown.get();
							inside.push_back(desktop::drawing([opened_group](desktop::canvas& c, desktop::rect area) {
								                 desktop::draw_icon(c, opened_group ? "chevron_down" : "chevron_right", {area.x, area.y + (area.height - 16) / 2, 16, 16}, current_theme().ink);
							                 })
							                     .width(16)
							                     .height(24)
							                     .grow(0)
							                     .on_click([shown] { shown.set(!shown.peek()); }));
						}
						auto pick_object = [&p, spec, id, object] {
							if (!spec.object_pick.empty()) p.pick(spec.object_pick, object);
							if (!spec.pick.empty()) p.pick(spec.pick, id);
						};
						inside.push_back(name_of_row(p, object, object_name(p, o), name_style(hidden, true), spec.object_rename, pick_object, spec.fields));
						if (!spec.object_remove.empty()) {
							// A group's all it holds go with it, in one step.
							std::vector<std::string> gone{object};
							placed groups;
							for (const auto& h : held) {
								if (!h["group"].as_string().empty()) groups.group_of[h["id"].as_string()] = h["group"].as_string();
							}
							for (const auto& h : held) {
								if (groups.inside(h["id"].as_string(), object)) gone.push_back(h["id"].as_string());
							}
							inside.push_back(desktop::tool_button("delete", "Delete", [&p, command = spec.object_remove, gone] {
								bool first = true;
								for (const auto& each : gone) {
									value sent = value(std::map<std::string, value>{{"id", each}});
									if (!std::exchange(first, false)) sent.fields["one:history"] = value("joined");
									p.run(command, sent);
								}
							}, {.tiny = true, .on_hover = true}));
						}
						rows.push_back(list_row({}, std::move(inside), object == picked_object, indent, pick_object));
						movable.push_back(false), layer_of.push_back(i);
						if (group && shown.get()) add(object, indent + 16, depth + 1);
					}
				};
				add("", 20, 0);
			}
			// A layer held and dragged goes where it's let go, in one step.
			auto list = desktop::reorderable(std::move(rows), std::move(movable), [&p, command = spec.order, ordered, layer_of](std::size_t from, std::size_t to) {
				moved(p, command, ordered, layer_of[from], layer_of[to], {});
			});
			return desktop::column(layers_header(p, spec, groups), std::move(list).spacing(2).align(align::stretch)).spacing(4).align(align::stretch);
		}
	}  // namespace

	desktop::view layer_list(page& p, layers_spec spec) {
		const auto& t = current_theme();
		const view_state& v = *spec.view;
		if (v.status == status::loading) return desktop::text("Loading…", t.quiet());
		if (v.status == status::failed) return desktop::text(v.error.empty() ? "This couldn't be read." : v.error, t.quiet());
		const value data = collage_parts_of(v.data, spec.objects.empty() ? spec.list : spec.objects, spec.fields);
		double width = data[spec.width].as_double(), height = data[spec.height].as_double();
		if (!spec.objects.empty()) return held_layers(p, spec, data, width, height);
		const value& layers = data[spec.list];
		const auto& all = layers.as_array();
		if (all.empty()) return desktop::text("No layers yet.", t.quiet());
		std::string picked = spec.pick.empty() ? "" : p.picked_id(spec.pick);
		std::vector<desktop::view> rows;
		// Topmost first, as it's seen.
		for (std::size_t i = all.size(); i-- > 0;) {
			const value& l = all[i];
			std::string id = l["id"].as_string();
			bool hidden = hidden_layer(l);
			std::vector<desktop::view> row;
			desktop::view eye;
			if (!spec.hide.empty()) {
				eye = desktop::tool_button(hidden ? "eye_off" : "eye", hidden ? "Show" : "Hide",
				                           [&p, spec, id, hidden] { p.run(spec.hide, collage_fields_of(value(fields{{"id", id}, {"hidden", hidden ? 0 : 1}}), spec.fields)); }, {.tiny = true});
			}
			row.push_back(preview_of(p, value(std::vector<value>{l}), width, height, spec.clear, hidden));
			auto pick_row = [&p, pick = spec.pick, id] {
				if (!pick.empty()) p.pick(pick, id);
			};
			row.push_back(name_of_row(p, id, object_name(p, l), name_style(hidden, false), spec.rename, pick_row, spec.fields));
			if (!spec.remove.empty()) {
				row.push_back(desktop::tool_button("delete", "Delete", [&p, spec, id] { p.run(spec.remove, value(fields{{"id", id}})); }, {.tiny = true, .on_hover = true}));
			}
			rows.push_back(list_row(std::move(eye), std::move(row), id == picked, 0, [&p, pick = spec.pick, id] {
				if (!pick.empty()) p.pick(pick, id);
			}));
		}
		// A layer held and dragged goes where it's let go, in one step; rows are topmost first.
		std::vector<bool> movable(rows.size(), !spec.order.empty());
		std::size_t count = all.size();
		auto list = desktop::reorderable(std::move(rows), std::move(movable), [&p, spec, layers, count](std::size_t from, std::size_t to) {
			moved(p, spec.order, layers, count - 1 - from, count - 1 - to, spec.fields);
		});
		return desktop::column(layers_header(p, spec, {}), std::move(list).spacing(2).align(align::stretch)).spacing(4).align(align::stretch);
	}

}  // namespace one::desktop::runtime
