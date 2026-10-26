// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A collage: a picture made of layers, as big as it says, drawn scaled to fit on the
// workspace with room around it, or at its own size on black filling the screen
// (decision 0019). Its background sits as its view says, and what it leaves around
// it is the picture's edges drawn out, or its edge color; pictures are cropped,
// turned and mirrored as each layer says, and words drawn over a soft shadow. A click
// picks a layer, framed with its corners and the buttons over it; a picked layer is
// moved, or resized by a corner with its shape kept unless Shift is held, snapped
// unless Alt is, and the background slid where it can. Where everything goes is
// one::screens's collage model, the web's too.

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <mutex>

#include <one/screens/collage.hpp>

#include "one/desktop/runtime/components.hpp"

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

		bool is_words(const value& layer) { return layer["picture"].as_string().empty(); }

		// The lowest layer, when it's a picture: the background.
		bool is_background(const value& layers, const value& layer) {
			const auto& all = layers.as_array();
			return !all.empty() && !is_words(layer) && all.front()["id"].as_string() == layer["id"].as_string();
		}

		desktop::rect as_rect(const model::box& b) { return {static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.w), static_cast<float>(b.h)}; }

		desktop::color color_of(const std::string& written, desktop::color otherwise) {
			if (written.size() != 7 || written[0] != '#') return otherwise;
			char* end = nullptr;
			unsigned long hex = std::strtoul(written.c_str() + 1, &end, 16);
			return end && *end == 0 ? desktop::color::hex(static_cast<std::uint32_t>(hex)) : otherwise;
		}

		// Each picture read once, by where it's kept; drawn on more than one thread, so
		// one at a time reads.
		std::mutex reading;

		const desktop::picture* picture_kept(const std::filesystem::path& file) {
			static std::map<std::string, desktop::picture> read;
			std::lock_guard hold(reading);
			auto found = read.find(file.string());
			if (found == read.end()) {
				auto p = desktop::picture::read(file);
				found = read.emplace(file.string(), p ? *p : desktop::picture()).first;
			}
			return found->second ? &found->second : nullptr;
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

		// A layer's picture as it's shown: turned and mirrored as the layer says.
		const desktop::picture* layer_picture(page& p, const value& layer) {
			auto file = p.file_here(layer["picture"].as_string());
			if (!file) return nullptr;
			const desktop::picture* whole = picture_kept(*file);
			int turns = (static_cast<int>(layer["turns"].as_double()) % 4 + 4) % 4;
			bool mirrored = layer["mirrored"].as_double() != 0;
			if (!whole || (turns == 0 && !mirrored)) return whole;
			static std::map<std::string, desktop::picture> made;
			std::string key = std::format("{} {} {}", file->string(), turns, mirrored);
			std::lock_guard hold(reading);
			auto found = made.find(key);
			if (found == made.end()) found = made.emplace(key, oriented(*whole, turns, mirrored)).first;
			return &found->second;
		}

		// A background's edge color: the average of its outermost pixels, once.
		desktop::color edge_color(const std::string& kept, const desktop::picture& p) {
			static std::map<std::string, desktop::color> made;
			std::lock_guard hold(reading);
			if (auto found = made.find(kept); found != made.end()) return found->second;
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
			return made.emplace(kept, desktop::color::hex(channel(0) << 16 | channel(1) << 8 | channel(2))).first->second;
		}

		// The space a background leaves, `board` in the window, filled as the view says:
		// its outermost pixels drawn out to the edges, unless it says its edge color.
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
		desktop::text_style words_style(double size, int weight, desktop::color color) {
			return {.size = static_cast<float>(size), .weight = weight, .color = color, .line_height = 1.2f};
		}
		void paint_words(desktop::canvas& c, const value& layer, double size, desktop::point at) {
			int weight = layer["weight"].is_null() ? 700 : static_cast<int>(layer["weight"].as_double());
			std::string words = layer["words"].as_string().empty() ? " " : layer["words"].as_string();
			desktop::color shade = desktop::color::hex(0x000000);
			shade.alpha = 0.45f;
			float off = std::max(1.f, static_cast<float>(size) * 0.04f);
			c.text(desktop::lay_out(desktop::text_span{words, words_style(size, weight, shade)}, unwrapped, c.scale()), {at.x + off / 2, at.y + off});
			c.text(desktop::lay_out(desktop::text_span{words, words_style(size, weight, color_of(layer["color"].as_string(), desktop::color::hex(0xffffff)))}, unwrapped, c.scale()), at);
		}

		// What the pointer is doing to a layer, from where it was and where the pointer was.
		struct gesture {
			enum { none, moving, resizing, sliding } what = none;
			std::string id;
			model::box from;
			desktop::point at;
			int corner = 0;
			std::pair<double, double> across, down;  // how far a background slides
		};

		// Where the collage was last drawn in the window, and how much smaller than
		// itself; what the pointer is doing; where a layer is going, or was let go until
		// the view has it there; the lines it snaps to and snapped to; and the buttons
		// over the picked layer, as last drawn.
		struct placed {
			desktop::rect board{};
			float scale = 0;
			gesture now;
			std::optional<std::pair<std::string, model::box>> going;
			model::lines lines;
			std::optional<double> guide_x, guide_y;
			std::optional<std::pair<model::point, model::point>> corner;
			std::vector<std::pair<desktop::rect, std::size_t>> tools;
			model::box on_board(const model::box& b) const {
				return {board.x + b.x * scale, board.y + b.y * scale, b.w * scale, b.h * scale};
			}
			model::point on_collage(desktop::point at) const { return {(at.x - board.x) / scale, (at.y - board.y) / scale}; }
		};

		model::backdrop backdrop_of(const value& v, double width, double height) {
			auto or_else = [&](const char* f, double otherwise) { return v[f].is_null() ? otherwise : v[f].as_double(); };
			return {v["background"].as_string(), width, height, or_else("zoom", 1), or_else("slide_x", 0.5), or_else("slide_y", 0.5)};
		}

		// Where each layer is on the collage: the background where it sits, the rest as
		// they say, and one being moved or let go where it's going.
		model::box where_is(page& p, const value& v, const value& layers, const value& layer, double width, double height, placed& w) {
			model::box b = box_of(layer);
			if (is_background(layers, layer)) {
				if (const desktop::picture* shown = layer_picture(p, layer)) b = model::background_at(backdrop_of(v, width, height), shown->width(), shown->height());
			}
			if (auto& g = w.going; g && g->first == layer["id"].as_string()) {
				auto near = [](double a, double z) { return std::abs(a - z) < 0.01; };
				bool there = near(b.x, g->second.x) && near(b.y, g->second.y) && near(b.w, g->second.w) && near(b.h, g->second.h);
				if (there && w.now.what == gesture::none) g.reset();
				else b = g->second;
			}
			return b;
		}

		// The lines a layer snaps to: the collage's edges and middle, and every other layer's.
		void snap_lines(page& p, const value& v, const value& layers, double width, double height, placed& w, const std::string& moving) {
			std::vector<model::box> others;
			for (const auto& l : layers.as_array()) {
				if (l["id"].as_string() != moving) others.push_back(where_is(p, v, layers, l, width, height, w));
			}
			w.lines = model::snap_lines(width, height, others);
		}

		// Where a gesture takes its layer, the pointer now at `at` in the window.
		model::box going_to(placed& w, desktop::point at, double width, double height, bool snapping) {
			const gesture& g = w.now;
			double dx = (at.x - g.at.x) / w.scale, dy = (at.y - g.at.y) / w.scale, close = 8 / w.scale;
			bool snaps = snapping && !desktop::held().alt;
			model::guided to;
			if (g.what == gesture::moving) to = model::moved(g.from, dx, dy, w.lines, width, height, close, snaps);
			else if (g.what == gesture::sliding) to = model::slid(g.from, dx, dy, g.across, g.down, width, height, close, snaps);
			else to = model::resized(g.from, g.corner, dx, dy, !desktop::held().shift, w.lines, close, snaps);
			w.guide_x = to.guide_x, w.guide_y = to.guide_y, w.corner = to.corner;
			return to.at;
		}

	}  // namespace

	desktop::view collage(page& p, collage_spec spec) {
		auto w = p.kept<std::shared_ptr<placed>>("collage " + spec.list, std::make_shared<placed>()).peek();
		const view_state& v = *spec.view;
		// While its view isn't there to show, what it says instead.
		if (v.status == status::loading) return desktop::text("Loading…", current_theme().quiet());
		if (v.status == status::failed) return desktop::text(v.error.empty() ? "This couldn't be read." : v.error, current_theme().quiet());
		const value data = v.data;
		const value layers = data[spec.list];
		double width = data[spec.width].as_double(), height = data[spec.height].as_double();
		std::string picked = spec.pick.empty() ? "" : p.picked_id(spec.pick);
		bool full = fullscreen();
		// The buttons over the picked layer, as they are now.
		std::vector<std::pair<command_button, command_model>> over;
		if (!picked.empty() && !full) {
			for (const auto& b : spec.over) {
				if (auto shown = command_of(p, b)) {
					for (const auto& k : b.keys) p.on_key(k, [&p, name = b.command, fields = shown->fields] { p.run(name, fields); });
					over.emplace_back(b, std::move(*shown));
				}
			}
		}
		// Esc lets the picked layer go; the arrows nudge it, by ten with Shift.
		if (!picked.empty()) {
			p.on_key("escape", [&p, name = spec.pick] { p.pick(name, ""); });
			for (const auto& l : layers.as_array()) {
				if (l["id"].as_string() != picked || is_background(layers, l) || spec.place.empty()) continue;
				for (auto [key, dx, dy] : {std::tuple{"left", -1.0, 0.0}, {"right", 1.0, 0.0}, {"up", 0.0, -1.0}, {"down", 0.0, 1.0}}) {
					for (double by : {1.0, 10.0}) {
						std::string chord = by > 1 ? std::string("shift+") + key : key;
						p.on_key(chord, [&p, w, place = spec.place, id = picked, b = box_of(l), dx, dy, by] {
							model::box to{b.x + dx * by, b.y + dy * by, b.w, b.h};
							w->going = std::pair{id, to};
							p.run(place, value(fields{{"id", id}, {"x", to.x}, {"y", to.y}, {"w", to.w}, {"h", to.h}}));
						});
					}
				}
			}
		}
		auto paint = [&p, w, data, layers, width, height, picked, full, over, spec](desktop::canvas& c, desktop::rect area) {
			const auto& t = current_theme();
			c.box(area, {.fill = full ? desktop::color::hex(0x000000) : t.sunken});
			if (width <= 0 || height <= 0) return;
			float margin = full ? 0 : 32;
			float scale = std::min((area.width - 2 * margin) / static_cast<float>(width), (area.height - 2 * margin) / static_cast<float>(height));
			if (scale <= 0) return;
			float bw = static_cast<float>(width) * scale, bh = static_cast<float>(height) * scale;
			desktop::rect board{area.x + (area.width - bw) / 2, area.y + (area.height - bh) / 2, bw, bh};
			w->board = board;
			w->scale = scale;
			if (!full) {
				desktop::color shade = desktop::color::hex(0x000000);
				shade.alpha = 0.18f;
				c.shadow(board, 0, 14, shade, {0, 2});
			}
			c.box(board, {.fill = desktop::color::hex(0xffffff)});
			std::optional<desktop::rect> framed;
			bool framed_background = false;
			{
				auto inside = c.clip(board);
				for (const auto& layer : layers.as_array()) {
					std::string id = layer["id"].as_string();
					model::box b = where_is(p, data, layers, layer, width, height, *w);
					desktop::rect on = as_rect(w->on_board(b));
					if (is_words(layer)) {
						paint_words(c, layer, layer["size"].as_double() * scale, {on.x, on.y});
					} else if (const desktop::picture* shown = layer_picture(p, layer)) {
						bool background = is_background(layers, layer);
						if (background) paint_around(c, board, on, layer["picture"].as_string(), *shown, data["around"].as_string());
						c.picture(*shown, on, background ? desktop::rect{0, 0, 1, 1} : as_rect(crop_of(layer)));
					}
					if (id == picked && !full) {
						framed = on;
						framed_background = is_background(layers, layer);
					}
				}
			}
			// The lines a moved layer snapped to, across the whole collage.
			if (w->now.what != gesture::none) {
				if (w->corner) {
					auto [corner, own] = *w->corner;
					c.line({board.x + static_cast<float>(corner.x) * scale, board.y + static_cast<float>(corner.y) * scale},
					       {board.x + static_cast<float>(own.x) * scale, board.y + static_cast<float>(own.y) * scale}, 1.5f, t.accent);
				}
				if (w->guide_x) c.box({board.x + static_cast<float>(*w->guide_x) * scale - 0.5f, board.y, 1, bh}, {.fill = t.accent});
				if (w->guide_y) c.box({board.x, board.y + static_cast<float>(*w->guide_y) * scale - 0.5f, bw, 1}, {.fill = t.accent});
			}
			if (layers.as_array().empty() && !full && !spec.empty.empty()) {
				auto said = desktop::lay_out(desktop::text_span{spec.empty, t.quiet()}, bw - 32, c.scale());
				c.text(said, {board.x + (bw - said.width()) / 2, board.y + (bh - said.height()) / 2});
			}
			// The picked layer's frame and corners, over everything, and its buttons
			// just above it, or below when there's no room.
			w->tools.clear();
			if (!framed) return;
			c.box(*framed, {.border = t.accent, .border_width = 2});
			// The background is sized by how it sits, so it has no corners to drag; nor
			// do words, sized by their size.
			bool words = false;
			for (const auto& l : layers.as_array()) {
				if (l["id"].as_string() == picked) words = is_words(l);
			}
			if (!framed_background && !words && !spec.place.empty()) {
				for (auto at : {desktop::point{framed->x, framed->y}, {framed->right(), framed->y}, {framed->right(), framed->bottom()}, {framed->x, framed->bottom()}}) {
					c.box({at.x - handle / 2, at.y - handle / 2, handle, handle}, {.fill = desktop::color::hex(0xffffff), .border = t.accent, .border_width = 2, .radius = 2});
				}
			}
			if (over.empty()) return;
			constexpr float button = 30, gap = 2, pad = 4, apart = 12;
			float tools_w = static_cast<float>(over.size()) * button + static_cast<float>(over.size() - 1) * gap + 2 * pad, tools_h = button + 2 * pad;
			float x = std::clamp(framed->x + (framed->width - tools_w) / 2, area.x + 4, std::max(area.x + 4, area.right() - tools_w - 4));
			float y = framed->y - apart - tools_h;
			if (y < area.y + 4) y = std::min(framed->bottom() + apart, area.bottom() - tools_h - 4);
			desktop::rect bar{x, y, tools_w, tools_h};
			desktop::color shade = desktop::color::hex(0x000000);
			shade.alpha = 0.22f;
			c.shadow(bar, 8, 12, shade, {0, 2});
			c.box(bar, {.fill = t.surface, .border = t.line, .border_width = 1, .radius = 8});
			for (std::size_t i = 0; i < over.size(); ++i) {
				desktop::rect at{x + pad + static_cast<float>(i) * (button + gap), y + pad, button, button};
				const auto& b = over[i].first;
				bool away = b.icon == "delete";
				if (desktop::has_icon(b.icon)) desktop::draw_icon(c, b.icon, {at.x + 7, at.y + 7, 16, 16}, away ? t.danger : t.ink);
				else c.text(desktop::lay_out(desktop::text_span{b.label.substr(0, 1), t.strong()}, button, c.scale()), {at.x + 10, at.y + 6});
				w->tools.emplace_back(at, i);
			}
		};
		desktop::drawing_pointer pointer{
		    .on_press =
		        [&p, w, data, layers, width, height, picked, over, spec](desktop::point at, int) {
			        // Filling the screen, the collage is only looked at: a click goes back.
			        if (fullscreen()) {
				        fullscreen(false);
				        return true;
			        }
			        if (w->scale <= 0) return false;
			        // The buttons over the picked layer first.
			        for (const auto& [area, i] : w->tools) {
				        if (!area.contains(at)) continue;
				        p.run(over[i].first.command, over[i].second.fields);
				        return true;
			        }
			        model::point on{at.x, at.y};
			        // A corner of the picked layer resizes it.
			        for (const auto& l : layers.as_array()) {
				        if (l["id"].as_string() != picked || is_background(layers, l) || is_words(l) || spec.place.empty()) continue;
				        model::box b = where_is(p, data, layers, l, width, height, *w);
				        if (int corner = model::corner_at(w->on_board(b), on, handle); corner >= 0) {
					        w->now = {.what = gesture::resizing, .id = picked, .from = b, .at = at, .corner = corner, .across = {}, .down = {}};
					        snap_lines(p, data, layers, width, height, *w, picked);
					        return true;
				        }
			        }
			        // The top layer under the pointer, never the background, is picked and moves.
			        const auto& all = layers.as_array();
			        for (std::size_t i = all.size(); i-- > 0;) {
				        if (i == 0 && !is_words(all[0])) break;
				        model::box b = where_is(p, data, layers, all[i], width, height, *w);
				        if (!w->on_board(b).contains(on)) continue;
				        std::string id = all[i]["id"].as_string();
				        if (!spec.pick.empty()) p.pick(spec.pick, id);
				        if (!spec.place.empty()) {
					        w->now = {.what = gesture::moving, .id = id, .from = b, .at = at, .corner = 0, .across = {}, .down = {}};
					        snap_lines(p, data, layers, width, height, *w, id);
				        }
				        return true;
			        }
			        // On the collage with nothing over it, the background is picked, and slides
			        // where it can; off it, nothing is.
			        bool on_board = w->board.contains(at);
			        if (!on_board || all.empty() || is_words(all.front())) {
				        if (!spec.pick.empty()) p.pick(spec.pick, "");
				        return false;
			        }
			        std::string id = all.front()["id"].as_string();
			        if (!spec.pick.empty()) p.pick(spec.pick, id);
			        model::box b = where_is(p, data, layers, all.front(), width, height, *w);
			        auto backdrop = backdrop_of(data, width, height);
			        if (!spec.slide.empty() && model::slides(backdrop, b)) {
				        w->now = {.what = gesture::sliding, .id = id, .from = b, .at = at, .across = model::slide_range(width, b.w), .down = model::slide_range(height, b.h)};
			        }
			        return true;
		        },
		    .on_drag =
		        [w, width, height, snap = spec.snap](desktop::point at) {
			        if (w->now.what == gesture::none) return;
			        w->going = std::pair{w->now.id, going_to(*w, at, width, height, snap)};
		        },
		    .on_release =
		        [&p, w, width, height, spec](desktop::point at) {
			        if (w->now.what == gesture::none) return;
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
				        if (!a) w->going.reset();
			        };
			        if (g.what == gesture::sliding) {
				        auto fraction = [](double at, std::pair<double, double> range) { return range.second > range.first ? (at - range.first) / (range.second - range.first) : 0.5; };
				        p.run(spec.slide, value(fields{{"id", spec.subject}, {"slide_x", fraction(to.x, g.across)}, {"slide_y", fraction(to.y, g.down)}}), back);
				        return;
			        }
			        p.run(spec.place, value(fields{{"id", g.id}, {"x", to.x}, {"y", to.y}, {"w", to.w}, {"h", to.h}}), back);
		        },
		    .cursor =
		        [&p, w, data, layers, width, height, picked, spec](desktop::point at) {
			        if (w->scale <= 0) return desktop::cursor::normal;
			        if (w->now.what == gesture::sliding) return desktop::cursor::grabbing;
			        for (const auto& [area, i] : w->tools) {
				        if (area.contains(at)) return desktop::cursor::pointer;
			        }
			        model::point on{at.x, at.y};
			        for (const auto& l : layers.as_array()) {
				        if (l["id"].as_string() != picked || is_background(layers, l) || is_words(l) || spec.place.empty()) continue;
				        int corner = model::corner_at(w->on_board(where_is(p, data, layers, l, width, height, *w)), on, handle);
				        if (corner == 0 || corner == 2) return desktop::cursor::resize_falling;
				        if (corner == 1 || corner == 3) return desktop::cursor::resize_rising;
			        }
			        const auto& all = layers.as_array();
			        for (std::size_t i = all.size(); i-- > 0;) {
				        if (i == 0 && !is_words(all[0])) break;
				        if (w->on_board(where_is(p, data, layers, all[i], width, height, *w)).contains(on)) return spec.place.empty() ? desktop::cursor::pointer : desktop::cursor::move;
			        }
			        if (!spec.slide.empty() && !all.empty() && !is_words(all.front()) && w->board.contains(at) &&
			            model::slides(backdrop_of(data, width, height), where_is(p, data, layers, all.front(), width, height, *w))) {
				        return desktop::cursor::grab;
			        }
			        return desktop::cursor::normal;
		        },
		};
		if (!spec.fills) return desktop::drawing(std::move(paint), std::move(pointer)).height(560);
		return desktop::drawing(std::move(paint), std::move(pointer)).grow();
	}

}  // namespace one::desktop::runtime
