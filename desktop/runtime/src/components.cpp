// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What's on a screen, drawn: text and titles, the pages above, sections, links,
// commands, cards and details, each from what one::screens works out of it.

#include "one/desktop/picture.hpp"
#include "one/desktop/runtime/components.hpp"
#include "one/desktop/runtime/markdown.hpp"

#include <one/syntax.hpp>

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <set>
#include <span>
#include <thread>

namespace one::desktop::runtime {

	namespace {

		desktop::view quiet(std::string words) { return desktop::text(std::move(words), current_theme().quiet()); }

		// What cards open, in their order, kept where the page is left, for a record opened
		// from them to step through.
		void opened_among(page& p, const std::string& list, const std::vector<card_model>& cards, std::span<const value> rows = {}, const std::string& picture = "") {
			if (cards.empty() || cards.front().address.empty()) return;
			std::vector<std::string> addresses;
			addresses.reserve(cards.size());
			for (const auto& card : cards) addresses.push_back(card.address);
			p.where_left().lists[list] = std::make_shared<const std::vector<std::string>>(std::move(addresses));
			// And the picture each shows, for a slideshow to read the next one ahead.
			if (picture.empty() || rows.size() != cards.size()) return;
			std::vector<std::string> pictures;
			pictures.reserve(rows.size());
			for (const auto& row : rows) pictures.push_back(row[picture].as_string());
			p.where_left().pictures[list] = std::make_shared<const std::vector<std::string>>(std::move(pictures));
		}

		// Pictures come in, each a record made by the cards' add, given what the address
		// names, its file in the picture, and its own size, its file's name, bytes, date and
		// camera where the cards keep them; all one step to undo. The same as the web's.
		void add_pictures(page& p, const cards_spec& spec, const std::vector<coming_picture>& pictures) {
			for (const auto& picture : pictures) {
				value made = value::map();
				for (const auto& [field, id] : spec.given) made.fields[field] = value(id);
				auto keep = [&](const std::string& part, value held) {
					if (auto field = spec.fields.find(part); field != spec.fields.end()) made.fields[field->second] = std::move(held);
				};
				keep("picture_width", value(picture.width));
				keep("picture_height", value(picture.height));
				keep("name", value(picture_name(picture.file)));
				if (picture.bytes) keep("picture_bytes", value(*picture.bytes));
				if (!picture.date.empty()) keep("picture_date", value(picture.date));
				if (!picture.camera.empty()) keep("picture_camera", value(picture.camera));
				if (picture.joined) made.fields["one:history"] = value("joined");
				run_with_picture(p, spec.add, made, spec.fields.at("picture"), picture, [](const answer&) {});
			}
		}

		// What a mosaic's tile says over its picture's foot: its title, strong, and what
		// else it shows, light on a shade of the picture, so it reads on any.
		desktop::view caption_of(const card_model& card) {
			const auto& t = current_theme();
			auto title = t.strong();
			title.color = color::hex(0xffffff);
			auto rest = t.quiet();
			rest.color = color::hex(0xffffff, 0.85f);
			std::vector<desktop::view> said;
			said.push_back(desktop::text(card.title, title));
			for (const auto& d : card.details) said.push_back(desktop::text(d, rest));
			return desktop::column(std::move(said)).spacing(2).padding(edges(10, 12)).background({.fill = color::hex(0x000000, 0.45f)});
		}

		// Cards as a mosaic of their pictures, set as a printed page is: square cornered and
		// two pixels apart, each in the tile mosaic_tile gives it, filling it, cropped around
		// its middle, what else it shows across its foot. Its columns share its width, less
		// the gaps between, and its rows are as tall as a column is wide, so
		// it scales as it's resized and no card changes tile. Only what's in view, and as
		// much again around it, is drawn, each picture read and each caption made as it
		// comes into view, so a mosaic of many thousands is placed from its count alone. One
		// tile is chosen, ringed while the mosaic has the keyboard: moved with the arrows, to
		// the card before or after, or to the one above or below, and opened with Enter.
		// In columns instead, as columns_place lays them, each picture is its column's width
		// and as tall as its own shape, uncropped, placed from the sizes its row keeps.
		class mosaic_element : public element {
		public:
			struct tile {
				std::string picture, address;
				card_model card;
				double tall = 1;  // in columns, its height over its width
			};
			// `start` is the address of the tile chosen first, like the card a page was left
			// from, brought into view as it's first placed. `across` columns, or none for a mosaic.
			mosaic_element(page& p, std::vector<tile> tiles, const std::string& start, int across = 0)
			    : p_(p), tiles_(std::move(tiles)), captions_(tiles_.size()), across_(across) {
				for (std::size_t i = 0; i < tiles_.size(); ++i) {
					if (!start.empty() && tiles_[i].address == start) chosen_ = i, shown_ = false;
				}
				if (across_ > 0) {
					std::vector<double> tall;
					tall.reserve(tiles_.size());
					for (const auto& t : tiles_) tall.push_back(t.tall);
					laid_ = columns_place(tall, across_);
				}
			}

			bool focusable() const override { return !tiles_.empty(); }

			extent measure_content(float width) override {
				if (across_ > 0) {
					float side = column(width), tallest = 0;
					for (std::size_t c = 0; c < laid_.heights.size(); ++c)
						tallest = std::max(tallest, static_cast<float>(laid_.heights[c]) * side + std::max(0, laid_.counts[c] - 1) * gap);
					return {width, tallest};
				}
				int rows = mosaic_rows(tiles_.size());
				return {width, rows * column(width) + std::max(0, rows - 1) * gap};
			}

			void place_content(const rect& area) override {
				frames_.resize(tiles_.size());
				float side = column(area.width);
				for (std::size_t i = 0; i < tiles_.size(); ++i) {
					if (across_ > 0) {
						const auto& at = laid_.places[i];
						frames_[i] = {area.x + at.column * (side + gap), area.y + static_cast<float>(at.above) * side + at.before * gap, side, static_cast<float>(tiles_[i].tall) * side};
						continue;
					}
					auto at = mosaic_tile(i);
					frames_[i] = {area.x + at.column * (side + gap), area.y + at.row * (side + gap), at.columns * side + (at.columns - 1) * gap, at.rows * side + (at.rows - 1) * gap};
				}
				if (!std::exchange(shown_, true) && chosen_ < frames_.size()) reveal(frames_[chosen_]);
			}

			void paint_content(canvas& c) override {
				const auto& t = current_theme();
				rect view = c.clipped().value_or(frame);
				rect near{view.x, view.y - view.height, view.width, view.height * 3};
				// The tiles from the first block that reaches what's near, as they're placed in order.
				for (std::size_t i = first_near(near); i < tiles_.size(); ++i) {
					const rect& f = frames_[i];
					if (f.y > near.y + near.height) break;
					if (f.y + f.height < near.y) continue;
					c.box(f, {.fill = t.sunken});
					if (auto shown = picture_soon(p_, tiles_[i].picture, 640); shown && *shown) {
						float pw = static_cast<float>(shown->width()), ph = static_cast<float>(shown->height());
						float s = std::max(f.width / pw, f.height / ph);
						float fw = f.width / (pw * s), fh = f.height / (ph * s);
						c.picture(*shown, f, {(1 - fw) / 2, (1 - fh) / 2, fw, fh});
					}
					auto& caption = captions_[i];
					if (!caption) {
						caption = caption_of(tiles_[i].card).take();
						caption->parent = this;
					}
					extent e = caption->measure(f.width);
					caption->place({f.x, f.y + f.height - e.height, f.width, e.height});
					caption->paint(c);
				}
				// The ring inside the chosen tile, square, so it neither rounds nor spills into the gaps.
				if (shows_focus() && chosen_ < frames_.size()) c.box(frames_[chosen_].inset(1), {.border = t.accent, .border_width = 2});
			}

			bool handle_key(const key_event& e) override {
				if (e.alt || e.control || e.super || tiles_.empty()) return false;
				if (e.is(keys::enter) || e.is(keys::keypad_enter)) {
					if (!tiles_[chosen_].address.empty()) p_.open(tiles_[chosen_].address);
					return true;
				}
				// Past either end, the key is left for what's beyond, as a list leaves it.
				std::size_t to = chosen_;
				if (e.is(keys::right) || e.is('l')) {
					if (chosen_ + 1 == tiles_.size()) return false;
					to = chosen_ + 1;
				} else if (e.is(keys::left) || e.is('h')) {
					if (chosen_ == 0) return false;
					to = chosen_ - 1;
				} else if (e.is(keys::down) || e.is('j') || e.is(keys::up) || e.is('k')) {
					auto found = nearest(e.is(keys::down) || e.is('j'));
					if (!found) return false;
					to = *found;
				} else {
					return false;
				}
				chosen_ = to;
				if (chosen_ < frames_.size()) reveal(frames_[chosen_]);
				return true;
			}

			// A tile pressed and moved a few pixels is carried out, its picture's file, to
			// another program to let it go over, and isn't opened.
			bool press(point at, int) override {
				pressed_ = at;
				carried_ = false;
				return true;
			}
			void drag(point at) override {
				if (!pressed_ || std::hypot(at.x - pressed_->x, at.y - pressed_->y) < 6) return;
				point from = *std::exchange(pressed_, std::nullopt);
				for (std::size_t i = 0; i < frames_.size(); ++i) {
					if (!frames_[i].contains(from)) continue;
					carried_ = true;
					if (auto file = tiles_[i].picture.empty() ? std::nullopt : p_.file_here(tiles_[i].picture)) {
						auto shown = picture_soon(p_, tiles_[i].picture, 640);
						desktop::carry_out(*file, tiles_[i].card.title, shown ? *shown : desktop::picture());
					}
					return;
				}
			}
			void release(point, element*) override { pressed_.reset(); }

			bool click(point at) override {
				if (std::exchange(carried_, false)) return true;
				for (std::size_t i = 0; i < frames_.size(); ++i) {
					if (frames_[i].contains(at) && !tiles_[i].address.empty()) {
						chosen_ = i;
						p_.open(tiles_[i].address);
						return true;
					}
				}
				return false;
			}

			void adopt(element& before) override {
				element::adopt(before);
				auto& was = static_cast<mosaic_element&>(before);
				shown_ = was.shown_ || shown_;
				if (was.chosen_ >= was.tiles_.size()) return;
				// The same card, if it's still here; otherwise the one now where it was.
				chosen_ = std::min(was.chosen_, tiles_.empty() ? 0 : tiles_.size() - 1);
				for (std::size_t i = 0; i < tiles_.size(); ++i) {
					if (tiles_[i].address == was.tiles_[was.chosen_].address) chosen_ = i;
				}
			}

			element* hit(point at) override { return frame.contains(at) ? this : nullptr; }
			one::desktop::cursor cursor() const override { return cursor::pointer; }

		private:
			static constexpr float gap = 2;  // as photos on a printed page are apart
			float column(float width) const {
				int n = across_ > 0 ? across_ : 3;
				return std::max(0.f, (width - (n - 1) * gap) / n);
			}
			// The first tile of the first block of four whose rows reach what's near. In
			// columns, each goes no higher than the one before it, so it's the first whose top
			// reaches it, less the last before that in each column, which may reach down into it.
			std::size_t first_near(const rect& near) const {
				if (frames_.empty()) return 0;
				if (across_ > 0) {
					auto from = static_cast<std::size_t>(std::ranges::lower_bound(frames_, near.y, {}, [](const rect& r) { return r.y; }) - frames_.begin());
					std::vector<bool> seen(static_cast<std::size_t>(across_), false);
					int left = across_;
					while (from > 0 && left > 0) {
						auto c = static_cast<std::size_t>(laid_.places[from - 1].column);
						if (!seen[c]) seen[c] = true, --left;
						--from;
					}
					return from;
				}
				float side = column(frame.width - padding.left - padding.right);
				float top = frames_[0].y;
				if (side <= 0 || near.y <= top) return 0;
				auto block = static_cast<std::size_t>((near.y - top) / (2 * (side + gap)));
				return std::min(tiles_.size(), block > 0 ? (block - 1) * 4 : 0);
			}
			// The tile above or below the chosen one nearest it across.
			std::optional<std::size_t> nearest(bool below) const {
				if (chosen_ >= frames_.size()) return std::nullopt;
				const rect& from = frames_[chosen_];
				float across = from.x + from.width / 2;
				std::optional<std::size_t> best;
				float best_score = 0;
				for (std::size_t i = 0; i < frames_.size(); ++i) {
					const rect& r = frames_[i];
					float ahead = below ? r.y - from.bottom() : from.y - r.bottom();
					if (i == chosen_ || ahead < -1) continue;
					float score = std::max(0.f, ahead) + 2 * std::abs(r.x + r.width / 2 - across);
					if (!best || score < best_score) best = i, best_score = score;
				}
				return best;
			}
			page& p_;
			std::vector<tile> tiles_;
			std::vector<std::unique_ptr<element>> captions_;  // each made as it first comes into view
			std::vector<rect> frames_;
			std::size_t chosen_ = 0;
			bool shown_ = true;  // whether the chosen tile was brought into view, as it's first placed
			int across_ = 0;
			columns_laid laid_;
			std::optional<point> pressed_;  // where the pointer's button went down, until it's moved off
			bool carried_ = false;          // whether that press carried a tile out
		};

		// What cards under headings by a date and their timeline share, by the cards' key:
		// the cards, while they're there, for the timeline to read where each heading's
		// part of them lies and to scroll them.
		struct dated_link {
			element* cards = nullptr;
		};
		std::shared_ptr<dated_link> dated_link_of(page& p, const std::string& key) {
			return p.kept<std::shared_ptr<dated_link>>("dated " + key, std::make_shared<dated_link>()).peek();
		}

		// Cards under a heading for each year, month or day, each heading and what's under
		// it in turn: only those near what's in view are drawn.
		class dated_element : public element {
		public:
			explicit dated_element(std::shared_ptr<dated_link> link) : link_(std::move(link)) { link_->cards = this; }
			~dated_element() override {
				if (link_->cards == this) link_->cards = nullptr;
			}

			extent measure_content(float width) override {
				float height = 0;
				for (std::size_t i = 0; i < children.size(); ++i) height += children[i]->measure(width).height + (i % 2 == 0 ? 12 : i + 1 < children.size() ? 32 : 0);
				return {width, height};
			}

			void place_content(const rect& area) override {
				float y = area.y;
				for (std::size_t i = 0; i < children.size(); ++i) {
					float h = children[i]->measure(area.width).height;
					children[i]->place({area.x, y, area.width, h});
					y += h + (i % 2 == 0 ? 12 : 32);
				}
			}

			void paint_content(canvas& c) override {
				rect view = c.clipped().value_or(frame);
				for (auto& child : children) {
					if (child->frame.y > view.y + 2 * view.height || child->frame.bottom() < view.y - view.height) continue;
					child->paint(c);
				}
			}

			// How many headings it has, and where each and what's under it lie down the
			// window, from the heading's top to the foot of its last card.
			std::size_t parts() const { return children.size() / 2; }
			std::pair<float, float> part(std::size_t i) const { return {children[2 * i]->frame.y, children[2 * i + 1]->frame.bottom()}; }

		private:
			std::shared_ptr<dated_link> link_;
		};

		// A timeline of cards under headings by a date, drawn along them as a scrollbar is:
		// as tall as what scrolls them, its top their top. Each day that has any is a mark
		// from its right edge, where the day is in the cards, as long as mark_length says,
		// days closer than two pixels one mark of them all; each year a line across, with
		// its number under it; and where they're scrolled to, a line across in the accent.
		// Pressed or dragged, it scrolls the cards there as it moves, and the day there is
		// said beside it while it's pressed or pointed at. With the keyboard, the arrows,
		// j and k go to the heading after or before, Page Down and Page Up to the year,
		// and Home and End to either end.
		class ruler_element : public element {
		public:
			ruler_element(dated_ruler ruler, std::shared_ptr<dated_link> link) : ruler_(std::move(ruler)), link_(std::move(link)) {}

			bool focusable() const override { return !ruler_.days.empty(); }
			extent measure_content(float width) override { return {width, 0}; }
			element* hit(point at) override { return frame.contains(at) ? this : nullptr; }
			one::desktop::cursor cursor() const override { return cursor::pointer; }

			void paint_content(canvas& c) override {
				auto* cards = shown();
				if (!cards) return;
				const auto& t = current_theme();
				lay_out_marks(*cards, c.scale());
				// Clear of the years' numbers at its left.
				float wide = std::max(4.f, frame.width - 36);
				auto marking = t.muted;
				for (const auto& m : marks_) c.box({frame.right() - mark_length(m.count, wide), frame.y + m.y, mark_length(m.count, wide), 1}, {.fill = marking});
				// Each year's line, and its number under it where there's room since the last.
				float free = frame.y;
				auto year = t.ink;
				year.alpha = 0.5f;
				for (std::size_t i = 0; i < ruler_.years.size(); ++i) {
					float y = frame.y + days_[ruler_.years[i].first];
					c.box({frame.x, y, frame.width, 1}, {.fill = year});
					if (y + 3 < free) continue;
					c.text(years_[i], {frame.x + 4, y + 3});
					free = y + 3 + years_[i].height() + 2;
				}
				c.box({frame.x, frame.y + place_.offset / place_.length * frame.height - 1, frame.width, 2}, {.fill = t.accent, .radius = 1});
				if (shows_focus()) c.box(frame.inset(1), {.border = t.accent, .border_width = 2, .radius = t.corners});
				// The day there, beside it, while it's pressed or pointed at.
				auto at = held_ ? held_ : pointed_;
				if (!at || days_.empty()) return;
				auto said = t.quiet();
				said.color = t.page;
				auto named = lay_out(text_span{day_title(ruler_.days[day_at(at->y - frame.y)].day), said}, 0, c.scale());
				float w = named.width() + 16, h = named.height() + 8;
				rect beside{frame.x - 8 - w, std::clamp(at->y - h / 2, frame.y, std::max(frame.y, frame.bottom() - h)), w, h};
				c.later([named = std::move(named), beside, behind = t.ink](canvas& on) {
					on.box(beside, {.fill = behind, .radius = 6});
					on.text(named, {beside.x + 8, beside.y + 4});
				});
			}

			bool press(point at, int) override {
				held_ = at;
				go(at.y);
				return true;
			}
			void drag(point at) override {
				held_ = at;
				go(at.y);
			}
			void release(point, element*) override { held_.reset(); }
			void pointing(std::optional<point> at) override { pointed_ = at; }

			void adopt(element& before) override {
				element::adopt(before);
				auto& was = static_cast<ruler_element&>(before);
				held_ = was.held_;
				pointed_ = was.pointed_;
			}

			bool handle_key(const key_event& e) override {
				auto* cards = shown();
				if (e.alt || e.control || e.super || !cards) return false;
				float now = place_.offset;
				// Where each heading or year starts, the next past one just under the top.
				std::vector<float> stops;
				if (e.is(keys::down) || e.is('j') || e.is(keys::up) || e.is('k')) {
					for (std::size_t i = 0; i < cards->parts(); ++i) stops.push_back(cards->part(i).first - place_.top);
				} else if (e.is(keys::page_down) || e.is(keys::page_up)) {
					for (const auto& y : ruler_.years) stops.push_back(down_to(*cards, ruler_.days[y.first]));
				} else if (e.is(keys::home)) {
					stops.push_back(0);
				} else if (e.is(keys::end)) {
					stops.push_back(place_.length);
				} else {
					return false;
				}
				bool back = e.is(keys::up) || e.is('k') || e.is(keys::page_up);
				std::optional<float> to;
				for (float y : stops) {
					if (back ? y < now - 1 : (y > now + 24 || stops.size() == 1)) to = back ? y : to.value_or(y);
				}
				if (back && !to) to = 0;
				if (to) cards->scroll_to(*to);
				desktop::runtime::redraw();
				return true;
			}

		private:
			struct mark {
				float y;
				std::size_t count;
			};

			// The cards, while they're there, and where they're scrolled to.
			dated_element* shown() {
				auto* cards = static_cast<dated_element*>(link_->cards);
				if (!cards || cards->parts() == 0) return nullptr;
				auto place = cards->scrolled();
				place_ = place ? *place : element::scroll_place{0, cards->frame.height, cards->frame.height, cards->frame.y};
				place_.length = std::max({place_.length, place_.visible, 1.f});
				return cards;
			}

			// How far down all that scrolls the cards a day starts, from where they're laid out.
			float down_to(const dated_element& cards, const ruler_day& d) const {
				auto [top, bottom] = cards.part(std::min(d.group, cards.parts() - 1));
				return top + d.along * (bottom - top) - place_.top;
			}

			// Each day's place down it, and the marks, worked out again only when the
			// cards are laid out otherwise or it's another height.
			void lay_out_marks(const dated_element& cards, double scale) {
				std::array<float, 5> now{frame.height, place_.length, static_cast<float>(cards.parts()), cards.part(0).first - place_.top, cards.part(cards.parts() - 1).second - place_.top};
				if (now == laid_ && years_scale_ == scale) return;
				laid_ = now;
				days_.clear();
				marks_.clear();
				for (const auto& d : ruler_.days) {
					float y = std::round(down_to(cards, d) / place_.length * frame.height);
					days_.push_back(y);
					if (!marks_.empty() && y - marks_.back().y < 2) marks_.back().count += d.count;
					else marks_.push_back({y, d.count});
				}
				if (years_scale_ != scale) {
					years_scale_ = scale;
					years_.clear();
					for (const auto& y : ruler_.years) years_.push_back(lay_out(text_span{std::to_string(y.year), current_theme().quiet()}, 0, scale));
				}
			}

			// The day at a place down it: the last that starts there or above.
			std::size_t day_at(float y) const {
				auto after = std::upper_bound(days_.begin(), days_.end(), y + 0.5f);
				return after == days_.begin() ? 0 : static_cast<std::size_t>(after - days_.begin()) - 1;
			}

			void go(float y) {
				auto* cards = shown();
				if (!cards) return;
				cards->scroll_to(std::clamp((y - frame.y) / std::max(1.f, frame.height), 0.f, 1.f) * place_.length);
				desktop::runtime::redraw();
			}

			dated_ruler ruler_;
			std::shared_ptr<dated_link> link_;
			element::scroll_place place_;
			std::array<float, 5> laid_{};
			std::vector<float> days_;
			std::vector<mark> marks_;
			std::vector<text_layout> years_;
			double years_scale_ = 0;
			std::optional<point> held_, pointed_;
		};

		// The short line over a front page's title, small and in the accent.
		desktop::view overline(std::string words) {
			const auto& t = current_theme();
			auto style = t.quiet();
			style.weight = 600;
			style.color = t.accent;
			return desktop::text(std::move(words), style);
		}

		// What a view says while it isn't there to show.
		std::optional<desktop::view> not_shown(const view_state& v) {
			if (v.status == status::loading) return quiet("Loading…");
			if (v.status == status::failed) {
				auto s = current_theme().body();
				s.color = current_theme().danger;
				return desktop::text(v.error.empty() ? "This couldn't be read." : v.error, s);
			}
			return std::nullopt;
		}

		// Another site's address, in the browser, without waiting on it.
		void open_in_browser(const std::string& address) {
			pid_t child = 0;
			char opener[] = "xdg-open";
			std::string copied = address;
			char* arguments[] = {opener, copied.data(), nullptr};
			if (posix_spawnp(&child, opener, nullptr, nullptr, arguments, environ) == 0) {
				std::thread([child] { waitpid(child, nullptr, 0); }).detach();
			}
		}

	}  // namespace

	// The library's colors, as a section or a tag is drawn in.
	color section_color(const std::string& name) {
		static const std::map<std::string, std::uint32_t> colors{
		    {"red", 0xc2312d}, {"orange", 0xd4700b}, {"amber", 0xb7791f}, {"yellow", 0x9a6700}, {"green", 0x1a7f37}, {"teal", 0x0a7e7a},
		    {"blue", 0x2457d6}, {"indigo", 0x4f46e5}, {"violet", 0x8250df}, {"pink", 0xbf3989}, {"gray", 0x57606a},
		};
		auto found = colors.find(name);
		return found == colors.end() ? current_theme().accent : color::hex(found->second);
	}

	std::optional<color> tone_color(int tone) {
		const auto& t = current_theme();
		switch (tone) {
			case 1: return t.danger;
			case 2: return color::hex(0xd4700b);
			case 4: return t.success;
			case 5: return t.accent;
			default: return std::nullopt;
		}
	}

	std::string glyph_of(const std::string& icon) {
		static const std::map<std::string, std::string> glyphs{{"add", "+"},      {"edit", "✎"},   {"follow", "☆"}, {"following", "★"}, {"settings", "⚙"},
		                                                       {"workflow", "⇄"}, {"delete", "✕"}, {"copy", "⧉"},   {"search", "⌕"},    {"back", "‹"},
		                                                       {"link", "↗"},     {"table", "▤"},  {"board", "▥"},  {"done", "✓"},       {"archive", "⊟"}};
		auto found = glyphs.find(icon);
		return found == glyphs.end() ? "" : found->second;
	}

	desktop::view items(std::vector<desktop::view> all) {
		std::erase_if(all, [](const desktop::view& v) { return !v.get(); });
		return desktop::column(std::move(all)).spacing(14);
	}

	// Words with a link written in them, like [Daher](https://…), are Markdown.
	desktop::view text(std::vector<part> parts, bool quiet) {
		std::string words = said(parts);
		if (words.find("](") != std::string::npos) return markdown_view(words);
		return desktop::text(std::move(words), quiet ? current_theme().quiet() : current_theme().body());
	}

	desktop::view subtitle(page& p, std::vector<part> parts) {
		p.under_title(desktop::text(said(parts), current_theme().quiet()));
		return {};
	}

	desktop::view crumbs(page& p, std::vector<crumb> trail) {
		std::vector<desktop::view> shown;
		for (const auto& c : crumb_links(p, trail)) {
			if (!shown.empty()) shown.push_back(quiet("›"));
			shown.push_back(desktop::link(c.title, [&p, address = c.address] { p.open(address); }, current_theme().quiet()));
		}
		p.above_title(desktop::row(std::move(shown)).spacing(8));
		return {};
	}

	desktop::view section(section_title title, std::vector<desktop::view> inside) {
		const auto& t = current_theme();
		auto heading_style = t.strong();
		heading_style.size += 2;
		if (!title.color.empty()) heading_style.color = section_color(title.color);
		std::vector<desktop::view> parts;
		if (!title.overline.empty()) parts.push_back(overline(std::move(title.overline)));
		// A step's number, and an icon, go before the title, in its row.
		if (title.step > 0 || desktop::has_icon(title.icon)) {
			std::vector<desktop::view> heading;
			if (desktop::has_icon(title.icon)) heading.push_back(desktop::icon(title.icon, title.color.empty() ? t.accent : section_color(title.color), 20));
			if (title.step > 0) {
				auto number = heading_style;
				number.color = t.accent;
				heading.push_back(desktop::text((title.step < 10 ? "0" : "") + std::to_string(title.step), number));
			}
			heading.push_back(desktop::text(std::move(title.title), heading_style));
			parts.push_back(desktop::row(std::move(heading)).spacing(10));
		} else {
			parts.push_back(desktop::text(std::move(title.title), heading_style));
		}
		for (auto& v : inside) parts.push_back(std::move(v));
		return desktop::column(std::move(parts)).spacing(10).padding(edges(8, 0, 0, 0));
	}

	desktop::view statement(std::vector<part> parts) {
		return desktop::text(said(parts), current_theme().heading(26)).padding(edges(16, 0));
	}

	desktop::view code(std::string language, std::string source) {
		const auto& t = current_theme();
		return desktop::text(colored_code(without_license(std::move(source)), language, t.code(), t)).padding(edges(12, 16)).background({.fill = t.sunken, .radius = std::min(t.corners, 8.f)});
	}

	std::vector<text_span> colored_code(const std::string& words, const std::string& language, const text_style& look, const desktop::theme& t) {
		const auto* read = language.empty() ? nullptr : syntax::find(language);
		if (!read) return {{words, look}};
		const auto& under = t.surface;
		bool dark = under.red * 0.299f + under.green * 0.587f + under.blue * 0.114f < 0.5f;
		std::vector<text_span> out;
		std::size_t done = 0;
		for (const auto& s : syntax::highlight(words, *read)) {
			if (s.from > done) out.push_back({words.substr(done, s.from - done), look});
			text_style colored = look;
			colored.color = color::hex(syntax::color(s.kind, dark));
			out.push_back({words.substr(s.from, s.to - s.from), colored});
			done = s.to;
		}
		out.push_back({words.substr(done), look});
		return out;
	}

	// The part of the page a link last brought into view, and how many times one has.
	using anchor = std::pair<std::string, int>;

	desktop::view anchored(page& p, std::string name, desktop::view part) {
		auto [wanted, times] = p.kept<anchor>("anchor").get();
		if (wanted != name || !part.get()) return part;
		return std::move(part).key("#" + name + "-" + std::to_string(times)).brought_to_top();
	}

	namespace {

		// A slideshow on a page: whether it's been looked at since the page opened, and
		// the wait for its next step, while there is one.
		struct slideshow {
			bool begun = false;
			std::shared_ptr<bool> waiting;
		};
		// The one playing, by its page's, which is gone once the page is.
		struct playing_now {
			std::weak_ptr<slideshow> show;
			page* on = nullptr;
			std::string toggle;
		};
		playing_now playing;
		std::string stopped_now;  // the toggle the key or click being heard stopped
		std::string shown_next;   // where a slideshow last stepped, where it carries on

		// every 4 seconds while slideshow: while its toggle's on, the next step waited for
		// away from the window's thread, the picture it goes to read meanwhile. Opened
		// some other way than by its own step, the page starts with it off.
		void played(page& p, const step_spec& spec) {
			auto show = p.kept<std::shared_ptr<slideshow>>("slideshow " + spec.while_on, std::make_shared<slideshow>()).peek();
			bool on = p.toggled(spec.while_on);
			if (!std::exchange(show->begun, true) && on && shown_next != p.address()) {
				p.toggle(spec.while_on);
				on = false;
			}
			if (!on) {
				show->waiting.reset();
				return;
			}
			playing = {show, &p, spec.while_on};
			if (show->waiting || !posting()) return;
			show->waiting = std::make_shared<bool>(true);
			auto every = std::chrono::duration<double>(spec.every);
			std::thread([&p, spec, every, waited = std::weak_ptr(show->waiting)] {
				std::this_thread::sleep_for(every);
				post([&p, spec, waited] {
					// Still waited for, the page is there and the show's still on.
					if (!waited.lock()) return;
					p.kept<std::shared_ptr<slideshow>>("slideshow " + spec.while_on, std::make_shared<slideshow>()).peek()->waiting.reset();
					if (!p.toggled(spec.while_on)) return;
					std::string next = stepped_to(p, spec);
					// At the end of the list, it stops.
					if (next.empty()) {
						p.toggle(spec.while_on);
						return;
					}
					shown_next = next;
					stepping(p, next);
					p.open_instead(next);
				});
			}).detach();
		}

	}  // namespace

	void input_heard() {
		stopped_now.clear();
		auto show = playing.show.lock();
		if (show && playing.on->toggled(playing.toggle)) {
			show->waiting.reset();
			playing.on->toggle(playing.toggle);
			stopped_now = playing.toggle;
		}
		playing = {};
		shown_next.clear();
	}

	bool stopped_just_now(const std::string& toggle) { return !toggle.empty() && stopped_now == toggle; }

	desktop::view step(page& p, step_spec spec, std::string words, std::string icon, std::vector<std::string> keys) {
		std::string to = stepped_to(p, spec);
		bool off = to.empty() || (p.in_group() && p.off());
		if (spec.every > 0 && !spec.while_on.empty()) played(p, spec);
		// The whole picture of the record it goes to read ahead, so it's drawn at once.
		if (std::string picture = picture_stepped_to(p, spec, to); !picture.empty()) read_ahead(p, to, picture);
		// In the record's place, so going back goes where it was opened from.
		auto go = [&p, to] {
			if (to.empty()) return;
			stepping(p, to);
			p.open_instead(to);
		};
		// Where it goes, for a swipe across a picture alone to step there too.
		*p.kept<std::shared_ptr<std::string>>(spec.next ? "step next" : "step previous", std::make_shared<std::string>()).peek() = off ? "" : to;
		// By its key, it leaves a picture's bars hidden, as they were.
		if (!off) {
			for (const auto& k : keys) p.on_key(k, [go] {
				go();
				stepped();
			});
		}
		std::string drawn = desktop::has_icon(icon) ? icon : "";
		if (auto small = p.in_group()) return desktop::tool_button(drawn, *small ? hint_of(words, keys) : words, std::move(go), {.large = !*small, .off = off, .flashed = flashing(keys)});
		return desktop::tool_button(drawn, drawn.empty() ? words : hint_of(words, keys), std::move(go), {.off = off, .flashed = flashing(keys)});
	}

	desktop::view link(page& p, std::string words, std::string to, std::string icon, std::vector<std::string> keys) {
		bool off = p.in_group() && p.off();
		if (!off) {
			for (const auto& k : keys) p.on_key(k, [&p, address = address_of(p, to)] { p.open(address); });
		}
		// In a toolbar's group, a tool, as its buttons are.
		if (auto small = p.in_group()) {
			return desktop::tool_button(desktop::has_icon(icon) ? icon : "", *small ? hint_of(words, keys) : words, [&p, address = address_of(p, to)] { p.open(address); },
			                            {.large = !*small, .off = off, .flashed = flashing(keys)});
		}
		// One of the web's icons is a quiet button drawn as just that, as on the web.
		if (desktop::has_icon(icon) && !leaves(to) && !to.starts_with("#")) {
			return desktop::icon_button(icon, std::move(words), [&p, address = address_of(p, to)] { p.open(address); });
		}
		if (std::string g = glyph_of(icon); !g.empty()) words = g + " " + words;
		if (leaves(to)) return desktop::link(std::move(words), [to] { open_in_browser(to); });
		if (to.starts_with("#")) {
			return desktop::link(std::move(words), [&p, name = to.substr(1)] {
				auto at = p.kept<anchor>("anchor");
				at.set({name, at.peek().second + 1});
			});
		}
		return desktop::link(std::move(words), [&p, address = address_of(p, to)] { p.open(address); });
	}

	desktop::view hero(std::string title, std::vector<desktop::view> inside) { return hero(section_title{.title = std::move(title)}, std::move(inside)); }

	desktop::view hero(section_title title, std::vector<desktop::view> inside) {
		std::vector<desktop::view> parts;
		if (!title.overline.empty()) parts.push_back(overline(std::move(title.overline)));
		parts.push_back(desktop::text(std::move(title.title), current_theme().heading(32)));
		for (auto& v : inside) {
			if (v.get()) parts.push_back(std::move(v));
		}
		return desktop::column(std::move(parts)).spacing(14).padding(edges(16, 0, 24, 0));
	}

	desktop::view command(page& p, command_button b) {
		// In a toolbar, one that doesn't apply now is there, but off, so the toolbar keeps
		// its shape.
		bool off = p.in_group() && (p.off() || !b.when);
		if (p.in_group()) b.when = true;
		// With nothing to act on, one whose values are remembered for what's drawn next is
		// pressed while they're what's remembered.
		// A style given to several chosen, or a group, goes to each, in one step.
		std::vector<std::string> many = b.remembers && !b.picks.empty() ? styled(p, b.picks) : std::vector<std::string>{};
		bool only_remembers = b.remembers && b.id.empty() && many.empty();
		if (only_remembers) b.pressed = holds_last_given(p, b);
		auto shown = command_of(p, b);
		if (!shown) return {};
		std::string label_now = shown->label;
		// Asked first, its own words the answer that runs it.
		if (!b.confirm.empty() && !off) p.ask_before(b.command, b.confirm, b.label, b.destroys);
		auto give = [&p, many](const std::string& name, value sent) {
			if (many.empty()) return p.run(name, sent);
			bool joined = false;
			for (const auto& id : many) {
				sent.fields["id"] = value(id);
				if (std::exchange(joined, true)) sent.fields["one:history"] = value("joined");
				p.run(name, sent);
			}
		};
		auto run = [&p, give, name = b.command, fields = shown->fields, busy = shown->busy, then = b.then_open, remembers = b.remembers, only_remembers, several = !many.empty()] {
			if (busy) return;
			if (remembers) remember_given(p, name, fields);
			if (only_remembers) return;
			if (several) return give(name, fields);
			if (then.empty()) return p.run(name, fields);
			// What it made, opened: its id in the route's last parameter.
			p.run(name, fields, [&p, then, name](const answer& made) {
				if (made) p.open(made_opens(then, name, *made, p.params()));
			});
		};
		if (!off) {
			for (const auto& k : b.keys) p.on_key(k, run);
		}
		// Along a slider: its words, then the slider from what it is now, the command run
		// with where it's let go.
		if (b.slide) {
			const command_slide& s = *b.slide;
			value was = only_remembers ? last_given(p, b.command).fields[s.field] : s.now;
			double now = std::clamp(!was.is_null() ? was.as_double() : !s.first.is_null() ? s.first.as_double() : s.to, s.from, s.to);
			state<std::string> held(std::format("{}", now));
			auto settled = [&p, give, name = b.command, fields = shown->fields, field = s.field, off, remembers = b.remembers, only_remembers](const std::string& text) {
				if (off) return;
				value sent = fields.is_null() ? value::map() : fields;
				sent.fields[field] = value(std::strtod(text.c_str(), nullptr));
				if (remembers) remember_given(p, name, sent);
				if (!only_remembers) give(name, sent);
			};
			desktop::view slid = desktop::slider(held, {.from = s.from, .to = s.to, .by = s.by, .percent = s.from == 0 && s.to == 1}, std::move(settled));
			auto words = current_theme().quiet();
			return desktop::row(desktop::text(label_now, words), std::move(slid).grow()).spacing(10).align(align::center).padding(edges(6, 8)).width(260);
		}
		// Any color: its words over a color picker, from what it is now, the command run
		// with each one chosen.
		if (b.color) {
			std::string now = only_remembers ? last_given(p, b.command).fields[b.color->field].as_string() : b.color->now.as_string();
			state<std::string> held(now.size() == 7 && now[0] == '#' ? now : std::string("#000000"));
			auto settled = [&p, give, name = b.command, fields = shown->fields, field = b.color->field, off, remembers = b.remembers, only_remembers](const std::string& hex) {
				if (off) return;
				value sent = fields.is_null() ? value::map() : fields;
				sent.fields[field] = value(hex);
				if (remembers) remember_given(p, name, sent);
				if (!only_remembers) give(name, sent);
			};
			return desktop::column(desktop::text(label_now, current_theme().quiet()), desktop::color_picker(held, std::move(settled))).spacing(6).padding(edges(6, 8)).width(240);
		}
		// Among a tool's options, a choice: what it gives drawn small, a page of its shape
		// or its icon, then its words and what it's for under them; tinted while it's what
		// holds now, and closing the options once it's chosen.
		if (const auto& open = p.in_options()) {
			const auto& t = current_theme();
			std::string swatch = b.swatch, icon = b.icon;
			desktop::view drawn = desktop::drawing([swatch, icon](desktop::canvas& c, desktop::rect area) {
				                      const auto& th = current_theme();
				                      if (swatch.starts_with("page ")) {
					                      double w = 0, h = 0;
					                      std::sscanf(swatch.c_str() + 5, "%lf %lf", &w, &h);
					                      if (w <= 0 || h <= 0) return;
					                      float s = static_cast<float>(std::min(area.width / w, area.height / h)) * 0.86f;
					                      float pw = static_cast<float>(w) * s, ph = static_cast<float>(h) * s;
					                      c.box({area.x + (area.width - pw) / 2, area.y + (area.height - ph) / 2, pw, ph}, {.fill = th.surface, .border = th.ink, .border_width = 1.25f, .radius = 2});
				                      } else if (desktop::has_icon(icon)) {
					                      desktop::draw_icon(c, icon, {area.x + 4, area.y + 4, area.width - 8, area.height - 8}, th.ink);
				                      }
			                      })
			                          .width(32)
			                          .height(32)
			                          .grow(0);
			auto label_style = t.body();
			label_style.weight = b.pressed ? 600 : 400;
			std::vector<desktop::view> words;
			words.push_back(desktop::text(label_now, label_style));
			if (!b.about.empty()) words.push_back(desktop::text(b.about, t.quiet()));
			desktop::color tint = t.accent;
			tint.alpha = 0.1f;
			return desktop::row(std::move(drawn), desktop::column(std::move(words)).spacing(0).grow())
			    .spacing(10)
			    .align(align::center)
			    .padding(edges(6, 8))
			    .background({.fill = b.pressed ? tint : desktop::color{0, 0, 0, 0}, .radius = 6})
			    .on_click([run, open = *open] {
				    run();
				    open.set(false);
			    });
		}
		// In a toolbar's group, a tool: its icon over its words, or its icon alone, or a
		// circle of the color it gives.
		if (auto small = p.in_group()) {
			// A color as a circle of it, or clear; a stroke's as a line of it; a width as a
			// line that wide, in ink.
			std::optional<desktop::color> swatch;
			std::string color = b.swatch.starts_with("line ") ? b.swatch.substr(5) : b.swatch;
			float line = b.swatch.starts_with("line ") ? 2.f : 0.f;
			if (b.swatch.starts_with("width ")) {
				line = std::clamp(std::strtof(b.swatch.c_str() + 6, nullptr), 1.f, 6.f);
				color = "";
				swatch = current_theme().ink;
			}
			if (color.size() == 7) swatch = desktop::color::hex(static_cast<std::uint32_t>(std::strtoul(color.c_str() + 1, nullptr, 16)));
			bool clear = color == "none";
			return desktop::tool_button(desktop::has_icon(b.icon) ? b.icon : "", *small || swatch || clear ? hint_of(label_now, b.keys) : label_now, std::move(run),
			                            {.large = !*small, .on = b.pressed && !off, .off = off, .swatch = swatch, .clear = clear, .line = line, .flashed = flashing(b.keys)});
		}
		if (desktop::has_icon(b.icon)) return desktop::icon_button(b.icon, label_now, std::move(run), b.primary);
		if (std::string g = glyph_of(b.icon); !g.empty()) label_now = g + " " + label_now;
		return desktop::button(label_now, std::move(run), b.primary);
	}

	desktop::view either(page& p, std::vector<command_button> ways) {
		if (ways.empty()) return {};
		auto holds = std::ranges::find_if(ways, [](const command_button& b) { return b.allowed && b.when; });
		return command(p, holds == ways.end() ? ways.front() : *holds);
	}

	desktop::view confirm(page& p, std::string command, std::string question) {
		p.ask_before(command, std::move(question), "", command.ends_with("::delete"));
		return {};
	}

	desktop::view filter_note(page& p) {
		std::string said_now = p.filtered();
		if (said_now.empty()) return {};
		const auto& t = current_theme();
		auto s = t.strong();
		s.color = t.accent;
		return desktop::row(desktop::row(desktop::text(said_now, s), desktop::link("Show all", [&p] { p.clear_filter(); }, t.quiet()))
		                        .spacing(12)
		                        .padding(edges(6, 12))
		                        .background({.fill = t.sunken, .radius = t.corners}));
	}

	namespace {
		desktop::view cards_shown(page& p, const cards_spec& spec, std::vector<card_model> made, std::span<const value> rows);
	}

	desktop::view cards(page& p, cards_spec spec) {
		// Pictures dropped, pasted or chosen, each a record, and a drop target even while
		// there are none.
		if (spec.paste && !spec.add.empty() && spec.fields.contains("picture")) {
			auto handed = pictures_handed(p, spec.key);
			if (handed.empty()) handed = pictures_dropped(p, spec.key);
			// Each picture kept once: one whose file is a card's already isn't another.
			if (!handed.empty()) {
				std::set<std::string> held;
				for (const auto& row : rows_of(*spec.view, spec.list)) {
					if (const value& file = field_of(row, spec.picture); file.is_string() && !file.as_string().empty()) held.insert(file.as_string());
				}
				pictures_coming(p, spec.key, std::move(handed), std::move(held));
			}
			if (auto come = pictures_come(p, spec.key); !come.empty()) add_pictures(p, spec, come);
			std::string key = spec.key;
			spec.paste = false;
			return pictures_target(p, cards(p, std::move(spec)), key);
		}
		const view_state& v = *spec.view;
		// Hidden when empty is hidden until there's something to show, not drawn while it loads.
		if (spec.hide_empty && v.status != status::ready) return {};
		if (auto instead = not_shown(v)) return std::move(*instead);
		const auto& t = current_theme();
		// Under a heading for each year, month or day, each part of the list as cards of its own.
		if (!spec.by.empty()) {
			const auto& rows = rows_of(v, spec.list);
			auto groups = time_groups(rows, spec.by, spec.unit);
			if (groups.empty()) return spec.hide_empty ? desktop::view() : !spec.add.empty() ? quiet("Nothing here yet. Drop pictures here to add them.").padding(edges(48, 0)) : quiet("Nothing here yet.");
			auto all = cards_of(p, spec);
			opened_among(p, "cards " + spec.list + " " + spec.link, all, rows, spec.picture);
			auto heading = t.strong();
			heading.size = 22, heading.line_height = 1.25f;
			std::vector<desktop::view> parts;
			for (const auto& g : groups) {
				parts.push_back(desktop::text(g.title, heading));
				std::vector<card_model> held(std::make_move_iterator(all.begin() + static_cast<std::ptrdiff_t>(g.first)), std::make_move_iterator(all.begin() + static_cast<std::ptrdiff_t>(g.first + g.count)));
				parts.push_back(cards_shown(p, spec, std::move(held), std::span(rows).subspan(g.first, g.count)));
			}
			auto made = std::make_unique<dated_element>(dated_link_of(p, spec.key));
			for (auto& part : parts) {
				made->children.push_back(std::move(part).take());
				made->children.back()->parent = made.get();
			}
			return desktop::view(std::move(made));
		}
		auto all = cards_of(p, spec);
		opened_among(p, "cards " + spec.list + " " + spec.link, all, rows_of(v, spec.list), spec.picture);
		if (all.empty()) return spec.hide_empty ? desktop::view() : !spec.add.empty() ? quiet("Nothing here yet. Drop pictures here to add them.").padding(edges(48, 0)) : quiet("Nothing here yet.");
		return cards_shown(p, spec, std::move(all), rows_of(v, spec.list));
	}

	namespace {

		// Cards made of the rows they show, in turn: as a mosaic, a gallery of pictures, or a list.
		desktop::view cards_shown(page& p, const cards_spec& spec, std::vector<card_model> made, std::span<const value> rows) {
			const auto& t = current_theme();
			if (spec.mosaic || spec.across > 0) {
				p.bleed();
				std::vector<mosaic_element::tile> tiles;
				for (std::size_t i = 0; i < made.size(); ++i) {
					std::string address = made[i].address;
					tiles.push_back({rows[i][spec.picture].as_string(), std::move(address), std::move(made[i]), spec.across > 0 ? tall_of(rows[i]) : 1});
				}
				// The card the page was left from, when it's back, chosen and given the keyboard.
				const std::string& left = p.where_left().opened;
				bool back = std::ranges::any_of(tiles, [&](const auto& tile) { return !left.empty() && tile.address == left; });
				desktop::view shown(std::make_unique<mosaic_element>(p, std::move(tiles), left, spec.across));
				return back ? std::move(shown).autofocus() : std::move(shown);
			}
			std::vector<desktop::list_row> shown;
			std::map<std::string, std::string> opens;
			for (std::size_t i = 0; i < made.size(); ++i) {
				const card_model& card = made[i];
				opens[card.id] = card.address;
				std::vector<desktop::view> inside;
				if (const value& shows = spec.picture.empty() ? value() : rows[i][spec.picture]; shows.is_array()) {
					std::vector<std::string> kept;
					for (const auto& one : shows.as_array()) kept.push_back(one.as_string());
					inside.push_back(pictures_of(p, kept, 0.75f, 960));
				} else if (!spec.picture.empty()) {
					inside.push_back(picture_of(p, shows.as_string(), spec.fit, 180, 640));
				}
				// Its title in the text's face, strong, as large as a small heading: a card's name,
				// not a heading over the page's parts.
				auto title = t.strong();
				title.size = 18, title.line_height = 1.3f;
				if (card.tags.empty()) {
					inside.push_back(desktop::text(card.title, title));
				} else {
					// Its tags beside its title, like Private.
					std::vector<desktop::view> top;
					top.push_back(desktop::text(card.title, title).grow());
					for (const auto& tag : card.tags) top.push_back(desktop::text(tag, t.quiet()).padding(edges(1, 8)).background({.fill = t.sunken, .border = t.line, .border_width = 1, .radius = 9}));
					inside.push_back(desktop::row(std::move(top)).spacing(6));
				}
				for (const auto& d : card.details) inside.push_back(quiet(d));
				if (spec.tally) inside.push_back(quiet(card.tally));
				if (!card.filters.empty()) {
					std::vector<desktop::view> filters;
					for (const auto& f : card.filters) {
						auto look = t.quiet();
						look.color = f.color.empty() ? t.accent : section_color(f.color);
						filters.push_back(desktop::link(f.label, [&p, address = f.address] { p.open(address); }, look));
					}
					inside.push_back(desktop::row(std::move(filters)).spacing(14));
				}
				shown.push_back({card.id, desktop::column(std::move(inside)).spacing(8).padding(16).background({.fill = t.surface, .border = t.line, .border_width = 1, .radius = t.corners})});
			}
			// The card the page was left from, when it's back, chosen and given the keyboard.
			const std::string& left = p.where_left().opened;
			std::string start;
			for (const auto& [id, address] : opens) {
				if (!left.empty() && address == left) start = id;
			}
			// Cards with pictures are a gallery, side by side as many as fit, as on the web; those
			// with several set together, as a collection's cover, are larger, stretched to fill
			// each line.
			if (!spec.picture.empty()) {
				bool several = std::ranges::any_of(rows, [&](const value& row) { return row[spec.picture].is_array(); });
				std::vector<desktop::view> tiles;
				for (auto& card : shown) {
					std::string address = opens[card.key];
					auto tile = desktop::pressable(std::move(card.shown), [&p, address] {
						if (!address.empty()) p.open(address);
					});
					if (!several) tile = std::move(tile).width(280);
					tiles.push_back(card.key == start ? std::move(tile).autofocus().brought_into_view() : std::move(tile));
				}
				if (several) return desktop::tiles(std::move(tiles), 480).spacing(16);
				return desktop::flow(std::move(tiles)).spacing(16);
			}
			auto listed = desktop::list(std::move(shown), {.on_open = [&p, opens](const std::string& id) {
				if (auto found = opens.find(id); found != opens.end() && !found->second.empty()) p.open(found->second);
			}, .start = start});
			return start.empty() ? std::move(listed) : std::move(listed).autofocus();
		}

	}  // namespace

	desktop::view dated_timeline(page& p, dated_timeline_spec spec) {
		const view_state& v = *spec.view;
		if (v.status != status::ready) return {};
		auto ruler = ruler_of(rows_of(v, spec.list), spec.by, spec.unit);
		if (ruler.days.empty()) return {};
		return desktop::view(std::make_unique<ruler_element>(std::move(ruler), dated_link_of(p, spec.cards))).width(72).grow();
	}

	desktop::view details(page&, state<view_state> from, std::vector<screens::column> fields, std::map<std::string, std::vector<screens::option>> choices,
	                      std::optional<tint_spec> tint, std::vector<std::string> labels,
	                       std::vector<std::string> files) {
		const view_state& v = *from;
		if (auto instead = not_shown(v)) return std::move(*instead);
		const auto& t = current_theme();
		std::vector<desktop::view> shown;
		for (const auto& line : details_of(v, fields, choices, tint)) {
			std::vector<desktop::view> value_parts;
			if (auto tone = tone_color(line.tone)) value_parts.push_back(desktop::edge(*tone));
			if (std::ranges::contains(labels, line.field)) {
				// Labels as the table draws them, each on its own, going on to a new line.
				std::vector<desktop::view> words_shown;
				for (const auto& word : field_of(v.data, line.field).as_array()) {
					if (screens::show(word).empty()) continue;
					auto look = t.quiet();
					look.color = t.accent;
					words_shown.push_back(desktop::text(screens::show(word), look).padding(edges(1, 7)).background({.fill = t.sunken, .radius = 9}));
				}
				value_parts.push_back(desktop::flow(std::move(words_shown)).spacing(4).grow());
			} else if (std::ranges::contains(files, line.field)) {
				value_parts.push_back(desktop::text(file_names(field_of(v.data, line.field)), t.body()).grow());
			} else {
				value_parts.push_back(desktop::text(line.value, t.body()).grow());
			}
			shown.push_back(desktop::row(quiet(line.label).width(108), desktop::row(std::move(value_parts)).spacing(8).grow()).spacing(8));
		}
		return desktop::column(std::move(shown)).spacing(8).padding(16).background({.fill = t.surface, .border = t.line, .border_width = 1, .radius = t.corners});
	}

}  // namespace one::desktop::runtime
