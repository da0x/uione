// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A collage: a picture made of layers, as big as it says, drawn scaled to fit on the
// workspace with room around it, or at its own size on black filling the screen
// (decision 0019). Its background sits as its view says, and what it leaves around
// it is the picture's edges drawn out, or its edge color; pictures are cropped,
// turned and mirrored as each layer says, and words drawn over a soft shadow. A click
// picks a layer, framed with its corners and the buttons over it; a picked layer is
// moved, or resized by a corner with its shape kept unless Shift is held, snapped
// unless Alt is, and the background slid where it can. A picture is cropped in place,
// its whole picture faint around what's kept, and words are written where they are,
// with a caret, and resized by their size. Where everything goes is one::screens's
// collage model, the web's too.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <set>

#include <one/screens/collage.hpp>

#include <unistd.h>

#include <fstream>
#include <thread>

#include <ember.hpp>

#include "one/desktop/runtime/components.hpp"
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
		int weight_of(const value& layer) { return layer["weight"].is_null() ? 700 : static_cast<int>(layer["weight"].as_double()); }

		// How big words are on the collage, in its own pixels.
		std::pair<double, double> measured(const std::string& words, double size, int weight) {
			auto laid = desktop::lay_out(desktop::text_span{words.empty() ? " " : words, words_style(size, weight, {})}, unwrapped, 1.0);
			return {std::max(1.f, laid.width()), std::max(1.f, laid.height())};
		}

		// Words drawn at `at` in the window, and where the caret stands among them.
		desktop::rect paint_words(desktop::canvas& c, const value& layer, const std::string& said, double size, desktop::point at, std::size_t caret = std::string::npos) {
			int weight = weight_of(layer);
			std::string words = said.empty() ? " " : said;
			desktop::color shade = desktop::color::hex(0x000000);
			shade.alpha = 0.45f;
			float off = std::max(1.f, static_cast<float>(size) * 0.04f);
			c.text(desktop::lay_out(desktop::text_span{words, words_style(size, weight, shade)}, unwrapped, c.scale()), {at.x + off / 2, at.y + off});
			auto laid = desktop::lay_out(desktop::text_span{words, words_style(size, weight, color_of(layer["color"].as_string(), desktop::color::hex(0xffffff)))}, unwrapped, c.scale());
			c.text(laid, at);
			auto end = laid.caret(std::min(caret, said.size()));
			float line = static_cast<float>(size) * 1.2f;
			return {at.x + end.x / static_cast<float>(c.scale()), at.y + static_cast<float>(end.line) * line, 2, line};
		}

		// What the pointer is doing to a layer, from where it was and where the pointer was.
		struct gesture {
			enum { none, moving, resizing, sliding, cropping, shifting } what = none;
			std::string id;
			model::box from;
			desktop::point at;
			int corner = 0;
			std::pair<double, double> across, down;  // how far a background slides
			int edges = 0;                           // a crop's edges dragged, or none to move it
			model::box kept_from = {};               // while a crop's picture shifts, what's kept as it was
		};

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
			};
			std::optional<writing> editing;
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
			if (g.what == gesture::cropping && w.crop) return model::cropped(g.from, g.edges, dx, dy, w.crop->whole);
			model::guided to;
			if (g.what == gesture::moving) to = model::moved(g.from, dx, dy, w.lines, width, height, close, snaps);
			else if (g.what == gesture::sliding) to = model::slid(g.from, dx, dy, g.across, g.down, width, height, close, snaps);
			else to = model::resized(g.from, g.corner, dx, dy, !desktop::held().shift, w.lines, close, snaps);
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
		void keep_crop(page& p, placed& w, const std::string& command) {
			if (!w.crop) return;
			const auto& e = *w.crop;
			model::box crop{(e.kept.x - e.whole.x) / e.whole.w, (e.kept.y - e.whole.y) / e.whole.h, e.kept.w / e.whole.w, e.kept.h / e.whole.h};
			w.going = std::pair{e.id, e.kept};
			p.run(command, value(fields{{"id", e.id}, {"crop_x", crop.x}, {"crop_y", crop.y}, {"crop_w", crop.w}, {"crop_h", crop.h},
			                            {"x", e.kept.x}, {"y", e.kept.y}, {"w", e.kept.w}, {"h", e.kept.h}}));
			w.crop.reset();
		}

		// Words written kept, as big as they now are; written away to nothing, gone.
		void finish_writing(page& p, placed& w, const value& layers, const collage_spec& spec) {
			if (!w.editing) return;
			auto e = *std::exchange(w.editing, std::nullopt);
			for (const auto& l : layers.as_array()) {
				if (l["id"].as_string() != e.id) continue;
				if (e.words.find_first_not_of(" \n\t") == std::string::npos) {
					if (!spec.remove.empty()) p.run(spec.remove, value(fields{{"id", e.id}}));
					if (!spec.pick.empty()) p.pick(spec.pick, "");
					return;
				}
				if (e.words == l["words"].as_string()) return;
				auto [mw, mh] = measured(e.words, l["size"].as_double(), weight_of(l));
				p.run(spec.write, value(fields{{"id", e.id}, {"words", e.words}, {"size", l["size"]}, {"x", l["x"]}, {"y", l["y"]}, {"w", mw}, {"h", mh}}));
			}
		}

		// A key while words are written: Enter or Esc keeps them, Shift+Enter starts a
		// new line, the arrows, Home and End move the caret, and the rest is typed.
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
			bool new_line = k.is(keys::enter) && k.shift;
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

		// Pictures dropped or pasted: the first on a collage with nothing on it covers it,
		// and the rest are as big as they are, smaller where they wouldn't fit, centered
		// where they were let go, each a little further along; all one step to undo.
		void add_pictures(page& p, const collage_spec& spec, const value& layers, double width, double height, const std::vector<std::filesystem::path>& files,
		                  std::optional<model::point> on) {
			if (spec.add.empty() || spec.paste_into.empty()) return;
			const auto& all = layers.as_array();
			double depth = !all.empty() && !all.back()["depth"].is_null() ? all.back()["depth"].as_double() + 1 : 0;
			bool first = true, covered = !all.empty();
			int at = 0;
			for (const auto& file : files) {
				auto read = desktop::picture::read(file);
				if (!read || !*read) {
					p.told().set(std::format("{} isn't a picture this can read.", file.filename().string()));
					continue;
				}
				double pw = read->width(), ph = read->height();
				model::box b;
				if (!covered) {
					b = {0, 0, width, height};
					covered = true;
				} else {
					double s = std::min({1.0, width / pw, height / ph});
					double cx = on ? on->x : width / 2, cy = on ? on->y : height / 2;
					b = {cx - pw * s / 2 + 40.0 * at, cy - ph * s / 2 + 40.0 * at, pw * s, ph * s};
					++at;
				}
				value made = value(fields{{"x", b.x}, {"y", b.y}, {"w", b.w}, {"h", b.h}});
				if (!spec.parent.empty()) made.fields[spec.parent] = value(spec.subject);
				if (!all.empty() && !all.back()["depth"].is_null()) made.fields["depth"] = value(depth++);
				if (!spec.sized_width.empty()) made.fields[spec.sized_width] = value(pw), made.fields[spec.sized_height] = value(ph);
				if (!std::exchange(first, false)) made.fields["one:history"] = value("joined");
				p.run(spec.add, made, attached_files{{spec.paste_into, {file.string()}}}, [](const answer&) {}, false);
			}
		}

		// Words pasted: a layer of them in the middle, a twelfth of its height.
		void add_words(page& p, const collage_spec& spec, const value& layers, double width, double height, const std::string& words) {
			if (spec.add.empty()) return;
			double size = std::max(8.0, std::round(height / 12));
			auto [mw, mh] = measured(words, size, 700);
			value made = value(fields{{"words", words}, {"size", size}, {"x", (width - mw) / 2}, {"y", (height - mh) / 2}, {"w", mw}, {"h", mh}});
			if (!spec.parent.empty()) made.fields[spec.parent] = value(spec.subject);
			const auto& all = layers.as_array();
			if (!all.empty() && !all.back()["depth"].is_null()) made.fields["depth"] = value(all.back()["depth"].as_double() + 1);
			p.run(spec.add, made);
		}

		// What's on the clipboard, pasted: files a file manager copied, or a picture, as
		// pictures; or words.
		void paste(page& p, const collage_spec& spec, const value& layers, double width, double height) {
			if (auto files = desktop::clipboard_files(); !files.empty()) add_pictures(p, spec, layers, width, height, files, std::nullopt);
			else if (auto bytes = desktop::clipboard_picture()) add_pictures(p, spec, layers, width, height, {kept_for_now(*bytes)}, std::nullopt);
			else if (auto words = desktop::clipboard_text(); words && words->find_first_not_of(" \n\t") != std::string::npos) add_words(p, spec, layers, width, height, *words);
			else p.told().set("There's nothing to paste. Copy a picture or some words first.");
		}

		// The collage drawn on nothing, `scale` times its own size, as it's kept: all of
		// it, or only one layer, as big as that layer, cropped as it is.
		desktop::picture drawn_whole(page& p, const value& data, const value& layers, double width, double height, double scale, const std::string& only = "") {
			model::box area{0, 0, width, height};
			for (const auto& l : layers.as_array()) {
				if (!only.empty() && l["id"].as_string() == only && !is_background(layers, l)) area = box_of(l);
			}
			int w = std::max(1, static_cast<int>(std::lround(area.w * scale))), h = std::max(1, static_cast<int>(std::lround(area.h * scale)));
			return desktop::picture::drawn(w, h, [&](desktop::canvas& c) {
				auto at = [&](const model::box& b) {
					return desktop::rect{static_cast<float>((b.x - area.x) * scale), static_cast<float>((b.y - area.y) * scale), static_cast<float>(b.w * scale), static_cast<float>(b.h * scale)};
				};
				desktop::rect board = at({0, 0, width, height});
				if (only.empty()) c.box(board, {.fill = desktop::color::hex(0xffffff)});
				for (const auto& layer : layers.as_array()) {
					if (!only.empty() && layer["id"].as_string() != only) continue;
					if (is_words(layer)) {
						desktop::rect on = at(box_of(layer));
						paint_words(c, layer, layer["words"].as_string(), layer["size"].as_double() * scale, {on.x, on.y});
						continue;
					}
					const desktop::picture* shown = layer_picture(p, layer);
					if (!shown) continue;
					if (is_background(layers, layer)) {
						desktop::rect on = at(model::background_at(backdrop_of(data, width, height), shown->width(), shown->height()));
						paint_around(c, board, on, layer["picture"].as_string(), *shown, data["around"].as_string());
						c.picture(*shown, on, {0, 0, 1, 1});
					} else {
						c.picture(*shown, at(box_of(layer)), as_rect(crop_of(layer)));
					}
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
		void save(page& p, const desktop::picture& drawn, const std::string& name) {
			std::filesystem::path folder = saving_folder();
			std::string stem = std::format("{}-{:%Y-%m-%d-%H%M}", file_name_of(name),
			                               std::chrono::zoned_time(std::chrono::current_zone(), std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now())));
			std::filesystem::path file = folder / (stem + ".png");
			for (int n = 2; std::filesystem::exists(file); ++n) file = folder / std::format("{}-{}.png", stem, n);
			p.told().set("Saving…");
			std::thread([told = p.told(), problem = p.problem(), drawn, folder, file] {
				std::error_code error;
				std::filesystem::create_directories(folder, error);
				auto written = drawn.write_png(file);
				post([told, problem, file, written] {
					if (written) told.set(std::format("Saved {} in {}.", file.filename().string(), file.parent_path().string()));
					else told.set(""), problem.set(written.error());
				});
			}).detach();
		}

		// What a collage's preview is drawn from, apart from the preview itself.
		std::string fingerprint(const value& data, const std::string& preview_into) {
			value plain = data;
			plain.fields.erase(preview_into);
			return std::format("{:016x}", std::hash<std::string>{}(ember::to_json(plain)));
		}

	}  // namespace

	desktop::view picture_of(page& p, const std::string& kept, bool fit, float height) {
		auto file = kept.empty() ? std::nullopt : p.file_here(kept);
		const desktop::picture* shown = file ? picture_kept(*file) : nullptr;
		return desktop::drawing([shown, fit](desktop::canvas& c, desktop::rect area) {
			       const auto& t = current_theme();
			       c.box(area, {.fill = t.sunken});
			       if (!shown || area.width <= 0 || area.height <= 0) return;
			       float pw = static_cast<float>(shown->width()), ph = static_cast<float>(shown->height());
			       float s = fit ? std::min(area.width / pw, area.height / ph) : std::max(area.width / pw, area.height / ph);
			       if (fit) {
				       c.picture(*shown, {area.x + (area.width - pw * s) / 2, area.y + (area.height - ph * s) / 2, pw * s, ph * s});
			       } else {
				       float fw = area.width / (pw * s), fh = area.height / (ph * s);
				       c.picture(*shown, area, {(1 - fw) / 2, (1 - fh) / 2, fw, fh});
			       }
		       })
		    .height(height);
	}

	desktop::view collage_button(page& p, collage_ask ask) {
		auto asked = p.kept<std::string>("collage do " + ask.key);
		auto press = [asked, does = ask.does] { asked.set(does); };
		for (const auto& k : ask.keys) p.on_key(k, press);
		if (auto small = p.in_group()) {
			return desktop::tool_button(desktop::has_icon(ask.icon) ? ask.icon : "", *small ? hint_of(ask.label, ask.keys) : ask.label, std::move(press), {.large = !*small});
		}
		return desktop::tool_button(desktop::has_icon(ask.icon) ? ask.icon : "", hint_of(ask.label, ask.keys), std::move(press));
	}

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
		std::optional<value> chosen;
		for (const auto& l : layers.as_array()) {
			if (l["id"].as_string() == picked) chosen = l;
		}
		// A picture whose own size isn't kept yet, like one kept before its size was, has
		// it filled in from the picture as it's turned, once, never a step undo takes back.
		if (!spec.update.empty() && !spec.sized_width.empty()) {
			auto asked = p.kept<std::shared_ptr<std::set<std::string>>>("collage sized " + spec.key, std::make_shared<std::set<std::string>>()).peek();
			for (const auto& l : layers.as_array()) {
				if (!l.fields.contains(spec.sized_width) || l[spec.sized_width].as_double() > 0 || asked->contains(l["id"].as_string())) continue;
				const desktop::picture* shown = l["picture"].as_string().empty() ? nullptr : layer_picture(p, l);
				if (!shown) continue;
				asked->insert(l["id"].as_string());
				p.run(spec.update, value(fields{{"id", l["id"]}, {spec.sized_width, shown->width()}, {spec.sized_height, shown->height()}, {"one:history", "apart"}}));
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
			if (now != drawn_from.peek() && still && settled.peek() == now) {
				drawn_from.set(now);
				auto small = drawn_whole(p, data, layers, width, height, spec.preview_wide / width);
				std::filesystem::path file = kept_for_now(small.png());
				p.run(spec.preview, value(fields{{"id", spec.subject}, {"one:history", "apart"}}), attached_files{{spec.preview_into, {file.string()}}},
				      [file](const answer&) { std::filesystem::remove(file); }, true);
			} else if (now != drawn_from.peek() && waiting.peek() != now) {
				waiting.set(now);
				std::thread([settled, now] {
					std::this_thread::sleep_for(std::chrono::milliseconds(800));
					post([settled, now] { settled.set(now); });
				}).detach();
			}
		}
		// A crop or words are only of the picked layer; picking another puts them away.
		if (w->crop && w->crop->id != picked) w->crop.reset();
		if (w->editing && w->editing->id != picked) finish_writing(p, *w, layers, spec);
		// A screenshot taken, added like any picture.
		if (auto shot = p.kept<std::string>("collage screenshot " + spec.key); !shot.peek().empty()) {
			std::filesystem::path taken = shot.peek();
			shot.set("");
			add_pictures(p, spec, layers, width, height, {taken}, std::nullopt);
		}
		// What its buttons asked of it: a crop begun, words written, or words added.
		if (auto asked = p.kept<std::string>("collage do " + spec.key); !asked.peek().empty()) {
			std::string does = asked.peek();
			asked.set("");
			if (does == "crop" && chosen && !is_words(*chosen) && !is_background(layers, *chosen) && !spec.crop.empty()) {
				model::box b = box_of(*chosen);
				w->crop = placed::crop_edit{picked, model::whole_of(b, crop_of(*chosen)), b};
			} else if (does == "edit" && chosen && is_words(*chosen) && !spec.write.empty()) {
				w->editing = placed::writing{picked, (*chosen)["words"].as_string(), false};
			} else if (does == "paste") {
				paste(p, spec, layers, width, height);
			} else if (does == "screenshot" && !spec.paste_into.empty()) {
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
			} else if (does == "copy" || does == "copy picked" || does == "save" || does == "save picked") {
				// Copied for another program to paste, or saved: all of it, or the picked layer.
				bool one = does.ends_with("picked"), copying = does.starts_with("copy");
				if (one && !chosen) p.told().set("Pick a layer first.");
				else if (layers.as_array().empty() || width <= 0 || height <= 0) p.told().set(copying ? "There's nothing to copy yet." : "There's nothing to save yet.");
				else {
					auto drawn = drawn_whole(p, data, layers, width, height, 1, one ? picked : "");
					if (copying) {
						desktop::copy_picture(drawn);
						p.told().set(one ? "Copied the layer. Paste it anywhere." : "Copied the picture. Paste it anywhere.");
					} else {
						// Named for what the view calls it, or for the app when it calls it nothing.
						std::string name = data["title"].as_string().empty() ? data["name"].as_string() : data["title"].as_string();
						if (name.empty()) name = app_name();
						save(p, drawn, one ? name + " layer" : name);
					}
				}
			} else if (does == "add" && !spec.add.empty()) {
				// Words in the middle, a twelfth of its height, all chosen so the first key
				// typed replaces them, on top of the rest.
				double size = std::max(8.0, std::round(height / 12));
				std::string words = "Your text";
				auto [mw, mh] = measured(words, size, 700);
				value made = value(fields{{"words", words}, {"size", size}, {"x", (width - mw) / 2}, {"y", (height - mh) / 2}, {"w", mw}, {"h", mh}});
				if (!spec.parent.empty()) made.fields[spec.parent] = value(spec.subject);
				const auto& all = layers.as_array();
				if (!all.empty() && !all.back()["depth"].is_null()) made.fields["depth"] = value(all.back()["depth"].as_double() + 1);
				p.run(spec.add, made, [&p, w, pick = spec.pick, words](const answer& a) {
					if (!a) return;
					if (!pick.empty()) p.pick(pick, *a);
					w->editing = placed::writing{*a, words, true};
				});
			}
		}
		// Words being written take every key; a crop is kept with Enter and put away
		// with Esc, before anything else.
		if (w->editing) {
			typing([&p, w, layers, spec](const key_event& k) {
				if (!w->editing) return false;
				if (k.is(keys::escape) || (k.is(keys::enter) && !k.shift)) {
					finish_writing(p, *w, layers, spec);
					return true;
				}
				return written(*w->editing, k);
			});
		} else if (w->crop) {
			p.on_key("enter", [&p, w, command = spec.crop] { keep_crop(p, *w, command); });
			p.on_key("escape", [w] { w->crop.reset(); });
		}
		// The buttons over the picked layer, as they are now; while cropping, keep and
		// put away instead.
		std::vector<std::pair<command_button, command_model>> over;
		if (!picked.empty() && !full && !w->crop && !w->editing) {
			for (const auto& b : spec.over) {
				if (auto shown = command_of(p, b)) {
					for (const auto& k : b.keys) p.on_key(k, [&p, name = b.command, fields = shown->fields] { p.run(name, fields); });
					over.emplace_back(b, std::move(*shown));
				}
			}
		}
		// The keys of what asks the collage over the picked layer, while each fits it.
		if (chosen && !w->crop && !w->editing) {
			auto asked = p.kept<std::string>("collage do " + spec.key);
			for (const auto& a : spec.asks) {
				bool fits = a.does == "crop" ? !is_words(*chosen) && !is_background(layers, *chosen) : a.does == "edit" ? is_words(*chosen) : true;
				if (!fits) continue;
				for (const auto& k : a.keys) p.on_key(k, [asked, does = a.does] { asked.set(does); });
			}
		}
		// Esc lets the picked layer go; the arrows nudge it, by ten with Shift.
		if (!picked.empty() && !w->crop && !w->editing) {
			p.on_key("escape", [&p, name = spec.pick] { p.pick(name, ""); });
			if (chosen && !is_background(layers, *chosen) && !spec.place.empty()) {
				for (auto [key, dx, dy] : {std::tuple{"left", -1.0, 0.0}, {"right", 1.0, 0.0}, {"up", 0.0, -1.0}, {"down", 0.0, 1.0}}) {
					for (double by : {1.0, 10.0}) {
						std::string chord = by > 1 ? std::string("shift+") + key : key;
						p.on_key(chord, [&p, w, place = spec.place, id = picked, b = box_of(*chosen), dx, dy, by] {
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
			bool framed_background = false, framed_words = false, chosen_picture = false;
			{
				auto inside = c.clip(board);
				for (const auto& layer : layers.as_array()) {
					std::string id = layer["id"].as_string();
					model::box b = where_is(p, data, layers, layer, width, height, *w);
					if (is_words(layer)) {
						// Being written: as typed so far, with its caret, all of it marked while
						// the first key would replace it. Resized: its letters as large.
						bool writing = w->editing && w->editing->id == id;
						std::string said = writing ? w->editing->words : layer["words"].as_string();
						double size = layer["size"].as_double();
						model::box kept = box_of(layer);
						if (!writing && kept.h > 0) size *= b.h / kept.h;
						auto [mw, mh] = measured(said, size, weight_of(layer));
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
					const desktop::picture* shown = layer_picture(p, layer);
					desktop::rect on = as_rect(w->on_board(b));
					bool background = is_background(layers, layer);
					if (shown && w->crop && w->crop->id == id) {
						// Being cropped: the whole picture faint, what's kept as it will be.
						const auto& e = *w->crop;
						c.picture(*shown, as_rect(w->on_board(e.whole)), {0, 0, 1, 1}, 0.35f);
						model::box from{(e.kept.x - e.whole.x) / e.whole.w, (e.kept.y - e.whole.y) / e.whole.h, e.kept.w / e.whole.w, e.kept.h / e.whole.h};
						on = as_rect(w->on_board(e.kept));
						c.picture(*shown, on, as_rect(from));
					} else if (shown) {
						if (background) paint_around(c, board, on, layer["picture"].as_string(), *shown, data["around"].as_string());
						c.picture(*shown, on, background ? desktop::rect{0, 0, 1, 1} : as_rect(crop_of(layer)));
					}
					if (id == picked && !full) framed = on, framed_background = background, chosen_picture = !background;
				}
			}
			// The lines a moved layer snapped to, across the whole collage.
			if (w->now.what == gesture::moving || w->now.what == gesture::resizing || w->now.what == gesture::sliding) {
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
			// The picked layer's frame and corners, over everything, a crop's edges with
			// handles too, and its buttons just above it, or below when there's no room.
			w->tools.clear();
			if (!framed) return;
			c.box(*framed, {.border = t.accent, .border_width = 2});
			std::vector<desktop::point> handles;
			bool corners = w->crop ? true : framed_words ? !spec.write.empty() : !framed_background && !spec.place.empty();
			if (corners && !w->editing) handles = {{framed->x, framed->y}, {framed->right(), framed->y}, {framed->right(), framed->bottom()}, {framed->x, framed->bottom()}};
			if (w->crop) {
				float mid_x = framed->x + framed->width / 2, mid_y = framed->y + framed->height / 2;
				handles.insert(handles.end(), {{mid_x, framed->y}, {framed->right(), mid_y}, {mid_x, framed->bottom()}, {framed->x, mid_y}});
			}
			for (auto at : handles) {
				c.box({at.x - handle / 2, at.y - handle / 2, handle, handle}, {.fill = desktop::color::hex(0xffffff), .border = t.accent, .border_width = 2, .radius = 2});
			}
			// While cropping, keep and put away; otherwise the buttons the screen gives.
			std::vector<std::pair<std::string, std::string>> drawn;  // icon, label
			if (w->crop) drawn = {{"check", "Keep"}, {"close", "Put away"}};
			else {
				for (const auto& a : spec.asks) {
					bool fits = a.does == "crop" ? chosen_picture : a.does == "edit" ? framed_words : true;
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
					        if (i == 0) keep_crop(p, *w, spec.crop);
					        else w->crop.reset();
					        return true;
				        }
				        // The asks that fit what's picked first, then the commands.
				        std::vector<std::string> fitting;
				        for (const auto& l : layers.as_array()) {
					        if (l["id"].as_string() != picked) continue;
					        for (const auto& a : spec.asks) {
						        bool fits = a.does == "crop" ? !is_words(l) && !is_background(layers, l) : a.does == "edit" ? is_words(l) : true;
						        if (fits) fitting.push_back(a.does);
					        }
				        }
				        if (i < fitting.size()) p.kept<std::string>("collage do " + spec.key).set(fitting[i]);
				        else p.run(over[i - fitting.size()].first.command, over[i - fitting.size()].second.fields);
				        return true;
			        }
			        model::point on{at.x, at.y};
			        // Words being written are kept by a press anywhere but on them.
			        if (w->editing) {
				        bool on_them = false;
				        for (const auto& l : layers.as_array()) {
					        if (l["id"].as_string() != w->editing->id) continue;
					        auto [mw, mh] = measured(w->editing->words, l["size"].as_double(), weight_of(l));
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
					        w->now = {.what = gesture::shifting, .id = w->crop->id, .from = w->crop->whole, .at = at, .corner = 0, .across = {}, .down = {}, .edges = 0, .kept_from = w->crop->kept};
					        return true;
				        }
				        if (edges < 0) {
					        keep_crop(p, *w, spec.crop);
					        return true;
				        }
				        w->now = {.what = gesture::cropping, .id = w->crop->id, .from = w->crop->kept, .at = at, .corner = 0, .across = {}, .down = {}, .edges = edges, .kept_from = {}};
				        return true;
			        }
			        // A corner of the picked layer resizes it: a picture with its shape kept,
			        // words by their size.
			        for (const auto& l : layers.as_array()) {
				        if (l["id"].as_string() != picked || is_background(layers, l)) continue;
				        if (is_words(l) ? spec.write.empty() : spec.place.empty()) continue;
				        model::box b = where_is(p, data, layers, l, width, height, *w);
				        if (is_words(l)) {
					        auto [mw, mh] = measured(l["words"].as_string(), l["size"].as_double(), weight_of(l));
					        b.w = mw, b.h = mh;
				        }
				        if (int corner = model::corner_at(w->on_board(b), on, handle); corner >= 0) {
					        w->now = {.what = gesture::resizing, .id = picked, .from = b, .at = at, .corner = corner, .across = {}, .down = {}, .edges = 0, .kept_from = {}};
					        snap_lines(p, data, layers, width, height, *w, picked);
					        return true;
				        }
			        }
			        // The top layer under the pointer, never the background, is picked and moves;
			        // pressed twice, a picture is cropped and words written.
			        const auto& all = layers.as_array();
			        for (std::size_t i = all.size(); i-- > 0;) {
				        if (i == 0 && !is_words(all[0])) break;
				        model::box b = where_is(p, data, layers, all[i], width, height, *w);
				        if (is_words(all[i])) {
					        auto [mw, mh] = measured(all[i]["words"].as_string(), all[i]["size"].as_double(), weight_of(all[i]));
					        b.w = mw, b.h = mh;
				        }
				        if (!w->on_board(b).contains(on)) continue;
				        std::string id = all[i]["id"].as_string();
				        if (!spec.pick.empty()) p.pick(spec.pick, id);
				        if (clicks >= 2) {
					        if (is_words(all[i]) && !spec.write.empty()) w->editing = placed::writing{id, all[i]["words"].as_string(), false};
					        else if (!is_words(all[i]) && !spec.crop.empty()) w->crop = placed::crop_edit{id, model::whole_of(box_of(all[i]), crop_of(all[i])), box_of(all[i])};
					        return true;
				        }
				        if (!spec.place.empty()) {
					        w->now = {.what = gesture::moving, .id = id, .from = b, .at = at, .corner = 0, .across = {}, .down = {}, .edges = 0, .kept_from = {}};
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
			        if (!spec.slide.empty() && model::slides(backdrop_of(data, width, height), b)) {
				        w->now = {.what = gesture::sliding, .id = id, .from = b, .at = at, .corner = 0, .across = model::slide_range(width, b.w), .down = model::slide_range(height, b.h),
				                  .edges = 0, .kept_from = {}};
			        }
			        return true;
		        },
		    .on_drag =
		        [w, width, height, snap = spec.snap](desktop::point at) {
			        if (w->now.what == gesture::none) return;
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
				        if (!a) w->going.reset();
			        };
			        if (g.what == gesture::sliding) {
				        auto fraction = [](double at, std::pair<double, double> range) { return range.second > range.first ? (at - range.first) / (range.second - range.first) : 0.5; };
				        p.run(spec.slide, value(fields{{"id", spec.subject}, {"slide_x", fraction(to.x, g.across)}, {"slide_y", fraction(to.y, g.down)}}), back);
				        return;
			        }
			        // Words resized are larger or smaller words, not stretched ones.
			        for (const auto& l : layers.as_array()) {
				        if (l["id"].as_string() == g.id && is_words(l) && g.what == gesture::resizing && g.from.h > 0) {
					        double size = l["size"].as_double() * to.h / g.from.h;
					        auto [mw, mh] = measured(l["words"].as_string(), size, weight_of(l));
					        p.run(spec.write, value(fields{{"id", g.id}, {"words", l["words"]}, {"size", size}, {"x", to.x}, {"y", to.y}, {"w", mw}, {"h", mh}}), back);
					        return;
				        }
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
			        if (w->crop) {
				        int edges = model::crop_edges_at(w->on_board(w->crop->kept), on, handle);
				        if (edges < 0) return w->on_board(w->crop->whole).contains(on) ? desktop::cursor::grab : desktop::cursor::normal;
				        if (edges == 0) return desktop::cursor::move;
				        if (edges == 3 || edges == 12) return desktop::cursor::resize_falling;
				        if (edges == 6 || edges == 9) return desktop::cursor::resize_rising;
				        return edges & 5 ? desktop::cursor::resize_across : desktop::cursor::resize_up_down;
			        }
			        if (w->editing) return desktop::cursor::text;
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
		desktop::view drawn = desktop::drawing(std::move(paint), std::move(pointer));
		// Files dropped on it, or a browser's picture, are pictures where they're let go.
		if (!spec.paste_into.empty()) {
			drawn = desktop::file_target(
			    std::move(drawn),
			    [&p, w, spec, layers, width, height](desktop::dropped what, desktop::point at) {
				    std::optional<model::point> on;
				    if (w->scale > 0 && w->board.contains(at)) on = w->on_collage(at);
				    if (!what.picture.empty()) what.files.push_back(kept_for_now(what.picture));
				    if (what.files.empty() && !what.elsewhere.empty()) {
					    p.told().set("That picture is on the web. Save it first, then drop it here.");
					    return;
				    }
				    add_pictures(p, spec, layers, width, height, what.files, on);
			    },
			    {.border = current_theme().accent, .border_width = 3});
		}
		if (!spec.fills) return std::move(drawn).height(560);
		return std::move(drawn).grow();
	}

}  // namespace one::desktop::runtime
