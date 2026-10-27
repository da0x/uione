// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A list's rows as one upright sheet of paper, written on in place: one element that
// lays out every block as its kind says, keeps the caret as the block it's in and a
// place in its words, and sends what's written to the app's commands. Typing is sent
// once it pauses or the caret leaves the block; Enter splitting a block, and Backspace
// joining one to the one before, are each one step undo takes back.

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <thread>

#include "one/desktop/editing.hpp"
#include "one/desktop/runtime/components.hpp"
#include "paper/kinds.hpp"

namespace one::desktop::runtime {

	namespace {

		using fields = std::map<std::string, value>;
		using paper_kinds::block;
		using paper_kinds::kind_of;
		using paper_kinds::laid;

		// A letter page's width, at a screen's 96 to the inch, and an inch of margin.
		constexpr float sheet_width = 816, sheet_margin = 72, around_sheet = 28;
		constexpr float letter_shape = 11.f / 8.5f;
		constexpr float page_gap = 28;     // between one page and the next

		// What's kept of the page while it's open, whatever builds it again.
		struct paper_state {
			std::string at;         // the block the caret is in, by its id
			bool first = false;     // the caret is on an empty page, where its first block will be
			editing edit{"", true};  // that block's words, being written
			// Words typed, by block, until the view has them; and what of them was sent.
			std::map<std::string, std::string> written, sent;
			// A block being made at this position, or with this id once it's answered, which
			// takes the caret once it's there.
			std::optional<double> coming;
			std::string coming_id;
			std::set<std::string> known;  // the blocks there before, so the one made is the one at its position that's new
			std::size_t coming_caret = 0;
			float aim = -1;   // the x a caret going up or down keeps to
			int pauses = 0;   // typing's pauses, so only the last one sends
			bool show_caret = false;  // the caret moved by a key, so it's brought into view once laid out
			bool jumped = false;      // and gone to a heading from the outline, so what follows it is shown too
			std::string jump;         // a heading picked in the outline, which the caret goes to
			bool placed = false;  // the caret put somewhere once, when the page first opens
			std::string composing;
			int composing_at = -1;
		};

		// The theme on warm paper, as under a reading lamp: a warm page, sheet, ink and
		// lines, as opencontrols' sepia reading mode has them; its accent its own.
		desktop::theme warmed(desktop::theme t) {
			t.page = color::hex(0xf3ece2);
			t.surface = color::hex(0xf8f3ed);
			t.sunken = color::hex(0xeae2d7);
			t.ink = color::hex(0x423024);
			t.muted = color::hex(0x6f5e52);
			t.line = color::hex(0xd6cec2);
			return t;
		}

		// The faces a page is set in that the app may not have: book's Literata and lyric's
		// Fraunces, upright and italic, built in and offered once (fonts/cut.py).
		void use_faces() {
			static bool used = [] {
				static constexpr unsigned char literata[] = {
#embed "../fonts/literata.ttf"
				};
				static constexpr unsigned char literata_italic[] = {
#embed "../fonts/literata-italic.ttf"
				};
				static constexpr unsigned char fraunces[] = {
#embed "../fonts/fraunces.ttf"
				};
				static constexpr unsigned char fraunces_italic[] = {
#embed "../fonts/fraunces-italic.ttf"
				};
				for (std::span<const std::uint8_t> face : {std::span<const std::uint8_t>(literata), std::span<const std::uint8_t>(literata_italic),
				                                           std::span<const std::uint8_t>(fraunces), std::span<const std::uint8_t>(fraunces_italic)}) {
					desktop::use_font(face);
				}
				return true;
			}();
			(void)used;
		}

		text_align align_of(const std::string& align) { return align == "center" ? text_align::center : align == "right" ? text_align::right : text_align::left; }

		// A place between two blocks' positions, or after the last.
		double between(double before, std::optional<double> after) { return after ? (before + *after) / 2 : before + 1; }

		class paper_element : public element, public text_entry {
		public:
			paper_element(page& p, paper_spec spec, std::shared_ptr<paper_state> s, std::vector<block> blocks, state<int> tick, state<int> due)
			    : p_(p), spec_(std::move(spec)), s_(std::move(s)), blocks_(std::move(blocks)), tick_(tick), due_(due),
			      look_(spec_.warm ? warmed(current_theme()) : current_theme()), zoom_(static_cast<float>(spec_.size)) {}

			bool focusable() const override { return !spec_.read_only; }
			bool prefers_focus() const override { return !spec_.read_only; }
			one::desktop::cursor cursor() const override { return spec_.read_only ? cursor::normal : cursor::text; }
			text_entry* entry() override { return this; }

			extent measure_content(float width) override {
				lay_out_blocks(width, 1);
				return {width, height_};
			}
			void place_content(const rect& area) override { lay_out_blocks(area.width, text_scale()); }

			void paint_content(canvas& c) override {
				const auto& t = look_;
				rect area = inner();
				lay_out_blocks(area.width, c.scale());
				rect sheet = sheet_in(area);
				// Each page, numbered at its foot on the right, in its margin.
				auto number = t.quiet();
				number.size *= fitted_;
				for (int n = 0; n < pages_; ++n) {
					point moved = page_shift(n, sheet.width);
					rect page{sheet.x + moved.x, sheet.y + static_cast<float>(n) * (page_height_ + page_gap) + moved.y, sheet.width, page_height_};
					c.box({page.x, page.y + 2, page.width, page.height}, {.fill = color{0, 0, 0, 0.06f}, .radius = 3});
					c.box(page, {.fill = t.surface, .border = t.line, .border_width = 1, .radius = 3});
					auto said = desktop::lay_out({std::to_string(n + 1), number}, 0, c.scale());
					float m = margin(page.width);
					c.text(said, {page.right() - m - said.width(), page.bottom() - m / 2 - said.height() / 2});
				}
				for (std::size_t i = 0; i < shown_.size(); ++i) paint_block(c, i);
				// Moved by a key: the caret brought into view, as a field's is; gone to a
				// heading, it and what follows it.
				if (std::exchange(s_->show_caret, false) && (focused || spec_.read_only)) {
					auto [x, y, h] = caret_place();
					reveal({x, y - 24, 2, h + (std::exchange(s_->jumped, false) ? 480 : 48)});
				}
			}

			// An input method's part.
			const std::string& surrounding(std::size_t& caret, std::size_t& anchor) const override {
				caret = s_->edit.caret();
				anchor = s_->edit.anchor();
				return s_->edit.text();
			}
			bool multiline() const override { return true; }
			rect caret_area() override {
				auto [x, y, h] = caret_place();
				return {x, y, 1, h};
			}
			void compose(std::string text, int caret) override {
				s_->composing = std::move(text);
				s_->composing_at = caret;
				tick_.set(tick_.peek() + 1);
			}
			void commit(std::string text, std::size_t erase_before, std::size_t erase_after) override {
				s_->composing.clear();
				changed([&] {
					s_->edit.erase_around(erase_before, erase_after);
					s_->edit.type(text);
				});
			}

			bool handle_key(const key_event& e) override {
				auto* here = current();
				if (!here || spec_.read_only) return false;
				s_->show_caret = true;
				bool word = e.control, extend = e.shift;
				std::size_t caret = s_->edit.caret(), size = s_->edit.text().size();
				bool selecting = s_->edit.selecting();
				if ((e.is(keys::enter) || e.is(keys::keypad_enter)) && !e.control) {
					if (e.shift) return typed("\n");
					// As its kind says: split, a new line, or out of it, its empty last line taken away.
					auto does = kind_of(here->kind).enter(s_->edit.text(), caret, selecting);
					if (does == paper_kinds::on_enter::new_line) return typed("\n");
					if (does == paper_kinds::on_enter::leave) changed([&] { s_->edit.erase_back(false); });
					split();
					return true;
				}
				if (e.is(keys::backspace)) {
					if (!selecting && caret == 0) return join_back(), true;
					return changed([&] { s_->edit.erase_back(word); });
				}
				if (e.is(keys::delete_key)) {
					if (!selecting && caret == size) return join_next(), true;
					return changed([&] { s_->edit.erase_forward(word); });
				}
				if (e.is(keys::left)) {
					if (!selecting && !extend && caret == 0) return go_to(index() - 1, false), true;
					return moved([&] { s_->edit.left(word, extend); });
				}
				if (e.is(keys::right)) {
					if (!selecting && !extend && caret == size) return go_to(index() + 1, true), true;
					return moved([&] { s_->edit.right(word, extend); });
				}
				if (e.is(keys::up) || e.is(keys::down)) return by_line(e.is(keys::up) ? -1 : 1, extend), true;
				if (e.is(keys::home)) return moved([&] { e.control ? s_->edit.start(extend) : line_edge(false, extend); });
				if (e.is(keys::end)) return moved([&] { e.control ? s_->edit.end(extend) : line_edge(true, extend); });
				if (e.control && !e.alt) {
					switch (e.symbol) {
						case 'a': return moved([&] { s_->edit.select_all(); });
						case 'c':
							if (selecting) copy_text(s_->edit.selected());
							return true;
						case 'x':
							if (!selecting) return true;
							copy_text(s_->edit.selected());
							return changed([&] { s_->edit.erase_back(false); });
						case 'v':
							if (auto pasted = clipboard_text(); pasted && !pasted->empty()) return typed(*pasted);
							return true;
						case 'z':
						case 'Z':
						case 'y':
							// Undone by the page, with what's typed sent first so it's undone too.
							send_all();
							return false;
					}
					return false;
				}
				if (!e.alt && !e.super && !e.text.empty() && static_cast<unsigned char>(e.text[0]) >= 0x20 && e.text[0] != 0x7f) return typed(e.text);
				return false;
			}

			bool press(point at, int clicks) override {
				if (spec_.read_only) return false;
				auto [i, offset] = hit_block(at);
				if (i >= shown_.size()) return true;
				if (i != index()) enter_block(i, offset);
				if (clicks == 2) s_->edit.select_word(offset);
				else if (clicks == 3) s_->edit.select_line(offset);
				else s_->edit.move_to(offset, false);
				s_->aim = -1;
				tick_.set(tick_.peek() + 1);
				return true;
			}

			void drag(point at) override {
				auto [i, offset] = hit_block(at);
				if (i != index()) return;
				s_->edit.move_to(offset, true);
				tick_.set(tick_.peek() + 1);
			}

		private:
			// The first page; the rest follow it down, a gap between each.
			rect sheet_in(const rect& area) const {
				float w = page_width(area.width), across = spec_.spread ? 2 * w + page_gap : w;
				return {area.x + (area.width - across) / 2, area.y + around_sheet, w, page_height_};
			}

			float margin(float) const { return sheet_margin * fitted_; }
			// What a kind's room is at the reader's size.
			std::pair<float, float> room(const std::string& kind) const {
				auto [above, below] = kind_of(kind).room();
				return {above * size(), below * size()};
			}
			paper_kinds::setting setting() const { return {.look = look_, .face = spec_.face, .zoom = size()}; }
			// Its pages are a letter page's size, made smaller, every part alike, when they're
			// wider than the room there is, as a book's pages are shown whole on a small
			// screen: showing two at a time, or a narrower window, moves nothing from one
			// page to another. Its words are the reader's size on them, larger or smaller.
			float size() const { return zoom_ * fitted_; }
			float fitting(float width) const {
				float across = spec_.spread ? 2.f : 1.f;
				float natural = sheet_width * across + (across - 1) * page_gap;
				return std::clamp((width - 2 * around_sheet) / natural, 0.05f, 1.f);
			}
			// How wide a page is, at the reader's size, made smaller to fit the room there is.
			float page_width(float width) const {
				return sheet_width * fitting(width);
			}
			// Where a page is drawn, from its top as if they ran down in one column: side by
			// side, two to a row.
			point page_shift(int n, float w) const {
				if (!spec_.spread) return {0, 0};
				return {static_cast<float>(n % 2) * (w + page_gap), static_cast<float>(n / 2 - n) * (page_height_ + page_gap)};
			}
			point shift_of(const auto& l) const { return page_shift(static_cast<int>(l.y / (page_height_ + page_gap)), l.page_width); }

			// Whether the caret is in a block, by its place among the blocks, which is its
			// place among those laid out too.
			bool editing_block(std::size_t i) const { return i < blocks_.size() && i == index(); }

			std::size_t index() const {
				if (s_->first) return 0;
				for (std::size_t i = 0; i < blocks_.size(); ++i) {
					if (blocks_[i].id == s_->at) return i;
				}
				return blocks_.size();
			}

			block* current() {
				std::size_t i = index();
				return i < blocks_.size() ? &blocks_[i] : nullptr;
			}

			void lay_out_blocks(float width, double scale) {
				if (laid_width_ == width && laid_scale_ == scale && laid_tick_ == tick_.peek()) return;
				laid_width_ = width, laid_scale_ = scale, laid_tick_ = tick_.peek();
				fitted_ = fitting(width);
				float w = page_width(width), m = margin(w);
				auto at = setting();
				shown_.clear();
				for (std::size_t i = 0; i < blocks_.size(); ++i) {
					const auto& kind = kind_of(blocks_[i].kind);
					laid l{.b = blocks_[i], .look = kind.words_look(at), .words = {}, .own = {}};
					l.look.size *= size();
					l.page_width = w;
					bool here = editing_block(i) && !spec_.read_only;
					std::string words = here ? s_->edit.text() : l.b.words;
					float indent = kind.indent() * size();
					l.x = m + indent;
					l.width = w - 2 * m - indent;
					{
						std::vector<text_span> spans;
						if (here && !s_->composing.empty()) {
							text_style composed = l.look;
							composed.underline = true;
							spans = {{words.substr(0, s_->edit.caret()), l.look}, {s_->composing, composed}, {words.substr(s_->edit.caret()), l.look}};
						} else {
							spans = {{words, l.look}};
						}
						l.words = desktop::lay_out(spans, l.width, scale, align_of(l.b.align));
						l.height = l.words->height();
					}
					// Then whatever else its kind has, as it is now.
					l.b.words = words;
					kind.lay_out(l, at, here);
					shown_.push_back(std::move(l));
				}
				// Then placed, page by page: a block that doesn't fit what's left of a page
				// starts the next, and a title, subtitle or heading goes with what it heads
				// when that wouldn't fit after it.
				page_height_ = w * letter_shape;
				float top = m, bottom = page_height_ - m;
				pages_ = 1;
				float y = top;
				for (std::size_t i = 0; i < shown_.size(); ++i) {
					laid& l = shown_[i];
					bool first = y == top;
					float above = first ? 0 : room(l.b.kind).first;
					float needs = above + l.height;
					if (kind_of(l.b.kind).leads() && i + 1 < shown_.size()) {
						const laid& next = shown_[i + 1];
						if (next.height <= bottom - top) needs += room(l.b.kind).second + room(next.b.kind).first + next.height;
					}
					if (!first && y + needs > bottom) {
						top += page_height_ + page_gap;
						bottom += page_height_ + page_gap;
						y = top;
						above = 0;
						++pages_;
					}
					l.y = y + above;
					y = l.y + l.height + room(l.b.kind).second;
				}
				int rows = spec_.spread ? (pages_ + 1) / 2 : pages_;
				height_ = static_cast<float>(rows) * page_height_ + static_cast<float>(rows - 1) * page_gap + 2 * around_sheet;
			}

			point origin() const {
				rect sheet = sheet_in(inner());
				return {sheet.x, sheet.y};
			}

			void paint_block(canvas& c, std::size_t i) {
				const auto& t = look_;
				const laid& l = shown_[i];
				point o = origin(), moved = shift_of(l);
				o = {o.x + moved.x, o.y + moved.y};
				float x = o.x + l.x, y = o.y + l.y + l.words_y;
				const auto& kind = kind_of(l.b.kind);
				auto at = setting();
				kind.paint_behind(c, l, {x, o.y + l.y}, at);
				bool here = editing_block(i) && focused && !spec_.read_only;
				if (l.words && l.raw) {
					float scale = static_cast<float>(l.words->scale());
					const auto& lines = l.words->lines();
					if (here && s_->edit.selecting()) {
						auto [from, to] = s_->edit.selection();
						color chosen = t.accent;
						chosen.alpha = 0.22f;
						auto start = l.words->caret(from), finish = l.words->caret(to);
						for (std::size_t n = start.line; n <= finish.line && n < lines.size(); ++n) {
							float left = n == start.line ? start.x : lines[n].left, right = n == finish.line ? finish.x : std::max(lines[n].left + lines[n].width, left) + 6 * scale;
							c.box({x + left / scale, y + lines[n].top / scale, (right - left) / scale, lines[n].height / scale}, {.fill = chosen});
						}
					}
					bool empty = (editing_block(i) ? s_->edit.text() : l.b.words).empty() && s_->composing.empty();
					// An empty page says where its first words go.
					if (empty && i == 0 && !spec_.empty.empty() && blocks_.size() == 1) {
						text_style hint = l.look;
						hint.color = t.muted;
						hint.color.alpha = 0.6f;
						c.text(desktop::lay_out({spec_.empty, hint}, l.width, c.scale(), align_of(l.b.align)), {x, y});
					} else {
						c.text(*l.words, {x, y});
					}
				}
				kind.paint_front(c, l, {x, o.y + l.y}, at);
				if (here) {
					auto [cx, cy, ch] = caret_place();
					c.box({cx - 0.75f, cy, 1.5f, ch}, {.fill = t.ink});
				}
			}

			// Where the caret stands: its x, its top and how tall it is, in the window.
			std::tuple<float, float, float> caret_place() {
				std::size_t i = index();
				if (i >= shown_.size() || !shown_[i].words) return {0, 0, 0};
				if (spec_.read_only) {
					point o = origin(), moved = shift_of(shown_[i]);
					return {o.x + moved.x + shown_[i].x, o.y + moved.y + shown_[i].y, shown_[i].height};
				}
				const laid& l = shown_[i];
				point o = origin(), moved = shift_of(l);
				o = {o.x + moved.x, o.y + moved.y};
				float scale = static_cast<float>(l.words->scale());
				std::size_t at = s_->edit.caret() + (s_->composing.empty() ? 0 : s_->composing_at < 0 ? s_->composing.size() : static_cast<std::size_t>(s_->composing_at));
				auto c = l.words->caret(at);
				const auto& lines = l.words->lines();
				float top = o.y + l.y + l.words_y;
				if (lines.empty()) return {o.x + l.x, top, l.look.size * l.look.line_height};
				const auto& line = lines[std::min(c.line, lines.size() - 1)];
				return {o.x + l.x + c.x / scale, top + line.top / scale, line.height / scale};
			}

			// The block under a point, or the nearest, and the place in its words.
			std::pair<std::size_t, std::size_t> hit_block(point at) {
				point o = origin();
				std::size_t best = shown_.size();
				float nearest = 1e9f;
				// The nearest block, down and then across, as its page is drawn.
				for (std::size_t i = 0; i < shown_.size(); ++i) {
					const laid& l = shown_[i];
					point moved = shift_of(l);
					float top = o.y + moved.y + l.y, bottom = top + l.height;
					float left = o.x + moved.x, right = left + l.page_width;
					float away = (at.y < top ? top - at.y : at.y > bottom ? at.y - bottom : 0) + (at.x < left ? left - at.x : at.x > right ? at.x - right : 0);
					if (away < nearest) nearest = away, best = i;
				}
				if (best >= shown_.size()) return {best, 0};
				const laid& l = shown_[best];
				point moved = shift_of(l);
				o = {o.x + moved.x, o.y + moved.y};
				if (!l.words) return {best, l.b.words.size()};
				float scale = static_cast<float>(l.words->scale());
				float y = std::clamp((at.y - o.y - l.y - l.words_y) * scale, 0.f, std::max(0.f, l.words->height() * scale - 1));
				return {best, l.words->offset_at((at.x - o.x - l.x) * scale, y)};
			}

			// Moved into another block, the one left sent as it's written.
			void enter_block(std::size_t i, std::size_t caret) {
				leave();
				if (i >= blocks_.size()) return;
				s_->at = blocks_[i].id;
				s_->first = false;
				s_->edit = editing(blocks_[i].words, true);
				s_->edit.move_to(std::min(caret, blocks_[i].words.size()), false);
				if (!spec_.pick.empty()) p_.pick(spec_.pick, s_->at);
			}

			void go_to(std::size_t i, bool at_start) {
				if (i >= blocks_.size()) return;
				enter_block(i, at_start ? 0 : blocks_[i].words.size());
				s_->aim = -1;
				tick_.set(tick_.peek() + 1);
			}

			void line_edge(bool end, bool extend) {
				std::size_t i = index();
				if (i >= shown_.size() || !shown_[i].words) return end ? s_->edit.end(extend) : s_->edit.start(extend);
				const auto& w = *shown_[i].words;
				auto c = w.caret(s_->edit.caret());
				const auto& line = w.lines()[std::min(c.line, w.lines().size() - 1)];
				s_->edit.move_to(w.offset_at(end ? 1e9f : -1e9f, line.top + line.height / 2), extend);
			}

			// A line up or down, as near the caret's x as there is; past a block's first
			// or last line, into the block before or after.
			void by_line(int direction, bool extend) {
				std::size_t i = index();
				if (i >= shown_.size() || !shown_[i].words) return;
				const laid& l = shown_[i];
				const auto& w = *l.words;
				auto c = w.caret(s_->edit.caret());
				if (s_->aim < 0) s_->aim = c.x;
				std::size_t last = w.lines().empty() ? 0 : w.lines().size() - 1;
				bool leaves = direction < 0 ? c.line == 0 : c.line >= last;
				if (!leaves) {
					const auto& line = w.lines()[c.line + direction];
					s_->edit.move_to(w.offset_at(s_->aim, line.top + line.height / 2), extend);
				} else if (extend) {
					direction < 0 ? s_->edit.start(true) : s_->edit.end(true);
				} else {
					std::size_t to = direction < 0 ? i - 1 : i + 1;
					if (direction < 0 && i == 0) return s_->edit.start(false);
					if (to >= shown_.size()) return s_->edit.end(false);
					float aim = s_->aim;
					const laid& next = shown_[to];
					std::size_t offset = next.b.words.size();
					if (next.words && !next.words->lines().empty()) {
						const auto& lines = next.words->lines();
						std::size_t n = direction < 0 ? lines.size() - 1 : 0;
						offset = next.words->offset_at(aim, lines[n].top + lines[n].height / 2);
					}
					enter_block(to, offset);
					s_->aim = aim;
				}
				tick_.set(tick_.peek() + 1);
			}

			template <class F>
			bool moved(F&& how) {
				how();
				s_->aim = -1;
				tick_.set(tick_.peek() + 1);
				return true;
			}

			template <class F>
			bool changed(F&& how) {
				std::string before = s_->edit.text();
				how();
				s_->aim = -1;
				if (s_->edit.text() != before) wrote();
				tick_.set(tick_.peek() + 1);
				return true;
			}

			bool typed(std::string_view words) {
				// A character and a space at a block's start, which its kind says makes it
				// another, like "# " a heading.
				block* here = current();
				if (words == " " && here && spec_.writes_kind && s_->edit.caret() == 1 && !s_->edit.selecting()) {
					if (auto becomes = kind_of(here->kind).shortcut(s_->edit.text()[0])) {
						std::string rest = s_->edit.text().substr(1);
						here->kind = *becomes;
						s_->edit.replace_all(rest);
						s_->edit.move_to(0, false);
						s_->written[here->id] = rest;
						s_->sent[here->id] = rest;
						p_.run(spec_.write, collage_fields_of(value(fields{{"id", here->id}, {"words", rest}, {"kind", here->kind}}), spec_.fields));
						tick_.set(tick_.peek() + 1);
						return true;
					}
				}
				return changed([&] { s_->edit.type(words); });
			}

			// What was typed, kept until the view has it, and sent once typing pauses; on an
			// empty page, the first block made with it.
			void wrote() {
				if (s_->first) {
					if (spec_.add.empty()) return;
					s_->first = false;
					s_->at = "";
					value made = value(fields{{"position", 1}, {"words", s_->edit.text()}, {"kind", blocks_.empty() ? std::string("title") : blocks_.front().kind}});
					if (!spec_.parent.empty()) made.fields[spec_.parent] = value(spec_.subject);
					auto s = s_;
					auto tick = tick_;
					s_->coming = 1;
					s_->coming_caret = s_->edit.caret();
					p_.run(spec_.add, collage_fields_of(made, spec_.fields), [s, tick](const answer& a) {
						if (!a) return;
						s->coming_id = *a;
						tick.set(tick.peek() + 1);
					});
					return;
				}
				block* here = current();
				if (!here) return;
				s_->written[here->id] = s_->edit.text();
				int pause = ++s_->pauses;
				auto due = due_;
				std::thread([due, pause] {
					std::this_thread::sleep_for(std::chrono::milliseconds(600));
					post([due, pause] { due.set(pause); });
				}).detach();
			}

			void send(const std::string& id) {
				auto w = s_->written.find(id);
				if (w == s_->written.end() || id.empty() || spec_.write.empty()) return;
				if (s_->sent[id] == w->second) return;
				s_->sent[id] = w->second;
				p_.run(spec_.write, collage_fields_of(value(fields{{"id", id}, {"words", w->second}}), spec_.fields));
			}

		public:
			void send_all() {
				for (const auto& [id, words] : s_->written) send(id);
			}

		private:
			void leave() {
				if (block* here = current()) send(here->id);
			}

		public:
			// Inserted from a toolbar: an empty block after the caret's, given what the button
			// gives, the caret then in it.
			void insert(const value& given) {
				if (spec_.add.empty()) return;
				std::size_t i = index();
				if (block* here = current()) send(here->id);
				double position = 1;
				if (i < blocks_.size() && !s_->first) position = between(blocks_[i].position, i + 1 < blocks_.size() ? std::optional(blocks_[i + 1].position) : std::nullopt);
				value made = given;
				made.fields["words"] = value("");
				made.fields["position"] = value(position);
				if (!spec_.parent.empty()) made.fields[spec_.parent] = value(spec_.subject);
				s_->coming = position;
				s_->coming_id.clear();
				s_->known.clear();
				for (const auto& b : blocks_) s_->known.insert(b.id);
				s_->coming_caret = 0;
				s_->first = false;
				p_.run(spec_.add, collage_fields_of(made, spec_.fields));
				tick_.set(tick_.peek() + 1);
			}

		private:
			// Enter: the words after the caret made a block of their own after this one, of
			// the same kind when they're the middle of it, or text after its end.
			void split() {
				block* here = current();
				if (!here || spec_.add.empty()) return;
				std::size_t i = index();
				std::string words = s_->edit.text();
				std::size_t at = s_->edit.selecting() ? s_->edit.selection().first : s_->edit.caret();
				std::string head = words.substr(0, at), tail = words.substr(s_->edit.selecting() ? s_->edit.selection().second : at);
				bool middle = !tail.empty();
				if (head != here->words || s_->sent[here->id] != head) {
					s_->written[here->id] = head;
					s_->sent[here->id] = head;
					p_.run(spec_.write, collage_fields_of(value(fields{{"id", here->id}, {"words", head}}), spec_.fields));
				}
				here->words = head;
				std::optional<double> next;
				if (i + 1 < blocks_.size()) next = blocks_[i + 1].position;
				double position = between(here->position, next);
				value made = value(fields{{"position", position}, {"words", tail}, {"kind", middle ? here->kind : std::string("text")}, {"align", middle ? here->align : std::string("")}});
				if (!middle && here->align != "" && here->align != "left" && here->kind == "text") made.fields["align"] = value(here->align);
				if (std::string(made["align"].as_string()).empty()) made.fields.erase("align");
				if (!spec_.parent.empty()) made.fields[spec_.parent] = value(spec_.subject);
				made.fields["one:history"] = value("joined");
				s_->coming = position;
				s_->coming_id.clear();
				s_->known.clear();
				for (const auto& b : blocks_) s_->known.insert(b.id);
				s_->coming_caret = 0;
				s_->edit = editing(head, true);
				p_.run(spec_.add, collage_fields_of(made, spec_.fields));
				tick_.set(tick_.peek() + 1);
			}

			// Backspace at a block's start: its words onto the end of the one before, and
			// it deleted. Before an equation, the caret only goes there.
			void join_back() {
				std::size_t i = index();
				if (i == 0 || i >= blocks_.size()) return;
				block& before = blocks_[i - 1];
				block& here = blocks_[i];
				if (!kind_of(before.kind).joins() || spec_.remove.empty()) {
					if (here.words.empty() && s_->edit.text().empty() && !spec_.remove.empty()) {
						p_.run(spec_.remove, value(fields{{"id", here.id}}));
					}
					return go_to(i - 1, false);
				}
				std::string words = before.words + s_->edit.text();
				std::size_t caret = before.words.size();
				s_->written.erase(here.id);
				s_->written[before.id] = words;
				s_->sent[before.id] = words;
				p_.run(spec_.write, collage_fields_of(value(fields{{"id", before.id}, {"words", words}}), spec_.fields));
				p_.run(spec_.remove, value(fields{{"id", here.id}, {"one:history", "joined"}}));
				before.words = words;
				s_->at = before.id;
				s_->edit = editing(words, true);
				s_->edit.move_to(caret, false);
				if (!spec_.pick.empty()) p_.pick(spec_.pick, s_->at);
				tick_.set(tick_.peek() + 1);
			}

			std::string here_kind() const {
				std::size_t i = index();
				return i < blocks_.size() ? blocks_[i].kind : "";
			}

			// Delete at a block's end: the next one's words onto this one's end.
			void join_next() {
				std::size_t i = index();
				if (i + 1 >= blocks_.size() || !kind_of(here_kind()).joins() || spec_.remove.empty()) return;
				go_to(i + 1, true);
				join_back();
			}

			page& p_;
			paper_spec spec_;
			std::shared_ptr<paper_state> s_;
			std::vector<block> blocks_;
			state<int> tick_, due_;
			desktop::theme look_;  // the theme, or it warmed, that it's drawn in
			float zoom_ = 1;       // the reader's size
			float fitted_ = 1;     // and how much smaller its pages are shown to fit
			float page_height_ = 0;
			int pages_ = 1;
			std::vector<laid> shown_;
			float height_ = 0, laid_width_ = -1;
			double laid_scale_ = 0;
			int laid_tick_ = -1;
		};

	}  // namespace

	namespace {
		// The sizes a reader's page goes between, a step at a time, as a book is zoomed.
		constexpr double sizes[] = {0.8, 0.9, 1, 1.15, 1.3, 1.5};
		constexpr int sizes_count = static_cast<int>(std::size(sizes));
		// The step it's at, kept for the reader on this machine, for every page alike.
		state<int> size_step(page& p) {
			auto kept = p.kept<int>("paper size", -1);
			if (kept.peek() < 0) {
				std::string said = screens::remembered::get("paper size").value_or("");
				int step = said.empty() ? 2 : std::clamp(std::atoi(said.c_str()), 0, sizes_count - 1);
				kept.set(step);
			}
			return kept;
		}
	}  // namespace

	desktop::view paper_size(page& p, paper_ask ask) {
		auto step = size_step(p);
		int now = step.get();
		auto press = [step, by = ask.sizes] {
			int next = std::clamp(step.peek() + by, 0, sizes_count - 1);
			step.set(next);
			screens::remembered::set("paper size", std::to_string(next));
		};
		bool off = (p.in_group() && p.off()) || (ask.sizes > 0 ? now >= sizes_count - 1 : now <= 0);
		if (!off) {
			for (const auto& k : ask.keys) p.on_key(k, press);
		}
		std::string icon = desktop::has_icon(ask.icon) ? ask.icon : "";
		if (auto small = p.in_group()) return desktop::tool_button(icon, *small ? hint_of(ask.label, ask.keys) : ask.label, std::move(press), {.large = !*small, .off = off});
		return desktop::tool_button(icon, hint_of(ask.label, ask.keys), std::move(press), {.off = off});
	}

	desktop::view paper_button(page& p, paper_ask ask) {
		auto asked = p.kept<std::optional<value>>("paper insert " + ask.key);
		auto press = [asked, given = ask.fields] { asked.set(given); };
		bool off = p.in_group() && p.off();
		if (!off) {
			for (const auto& k : ask.keys) p.on_key(k, press);
		}
		if (auto small = p.in_group()) {
			return desktop::tool_button(desktop::has_icon(ask.icon) ? ask.icon : "", *small ? hint_of(ask.label, ask.keys) : ask.label, std::move(press), {.large = !*small, .off = off});
		}
		return desktop::tool_button(desktop::has_icon(ask.icon) ? ask.icon : "", hint_of(ask.label, ask.keys), std::move(press));
	}

	desktop::view paper(page& p, paper_spec spec) {
		// Around the pages, and beside them, is the app's own theme; only the pages are warm.
		const auto& t = current_theme();
		const view_state& v = *spec.view;
		if (v.status == status::loading) return desktop::text("Loading…", t.quiet());
		if (v.status == status::failed) return desktop::text(v.error.empty() ? "This couldn't be read." : v.error, t.quiet());
		auto s = p.kept<std::shared_ptr<paper_state>>("paper " + spec.key, std::make_shared<paper_state>()).peek();
		auto tick = p.kept<int>("paper tick " + spec.key);
		auto due = p.kept<int>("paper due " + spec.key);
		tick.get();
		// Read as its parts, whichever of the app's fields keep them, in the order their
		// positions say.
		const value data = collage_parts_of(v.data, spec.list, spec.fields);
		spec.face = data["face"].as_string();
		spec.size = sizes[size_step(p).get()];
		if (spec.face == "book" || spec.face == "lyric") use_faces();
		std::vector<block> blocks;
		for (const auto& row : data[spec.list].as_array()) {
			blocks.push_back({row["id"].as_string(), row["kind"].as_string(), row["align"].as_string(), row["words"].as_string(), row["position"].as_double()});
		}
		std::ranges::stable_sort(blocks, {}, &block::position);
		// What the view has caught up with is no longer kept apart; what it hasn't yet is
		// shown as it was written.
		for (auto& b : blocks) {
			auto w = s->written.find(b.id);
			if (w == s->written.end()) continue;
			if (w->second == b.words && s->sent[b.id] == b.words) s->written.erase(w), s->sent.erase(b.id);
			else b.words = w->second;
		}
		// A block made where the caret was going takes it, once it's there.
		if (s->coming) {
			for (const auto& b : blocks) {
				bool made = s->coming_id.empty() ? b.position == *s->coming && !s->known.contains(b.id) : b.id == s->coming_id;
				if (!made || b.id.empty()) continue;
				s->at = b.id;
				// The first block of an empty page keeps what was typed while it was made.
				if (!s->coming_id.empty() && !s->edit.text().empty()) {
					if (s->edit.text() != b.words) s->written[b.id] = s->edit.text();
				} else {
					s->edit = editing(b.words, true);
					s->edit.move_to(std::min(s->coming_caret, b.words.size()), false);
				}
				s->coming.reset();
				s->coming_id.clear();
				if (!spec.pick.empty()) p.pick(spec.pick, s->at);
				break;
			}
		}
		bool there = std::ranges::any_of(blocks, [&](const block& b) { return b.id == s->at; });
		if (blocks.empty()) {
			// An empty page is one empty block, its first, where the caret waits.
			if (!s->coming) s->first = true, s->edit = s->first && !s->edit.text().empty() ? s->edit : editing("", true);
			blocks.push_back({"", "title", "", "", 1});
		} else if (!there && !s->coming) {
			// Opened, or the caret's block gone, as undo can take it: the caret at the end
			// of the last.
			s->first = false;
			s->at = blocks.back().id;
			s->edit = editing(blocks.back().words, true);
			s->edit.end(false);
			if (!spec.pick.empty()) p.pick(spec.pick, s->at);
		} else if (there && !s->written.contains(s->at)) {
			// Changed elsewhere, as undo or a toolbar does: the caret's words as they're kept.
			for (const auto& b : blocks) {
				if (b.id == s->at && b.words != s->edit.text()) {
					std::size_t caret = std::min(s->edit.caret(), b.words.size());
					s->edit.replace_all(b.words);
					s->edit.move_to(caret, false);
				}
			}
		}
		// A heading picked in the outline: the caret at its end, and it brought into view.
		if (!s->jump.empty()) {
			for (const auto& b : blocks) {
				if (b.id != s->jump) continue;
				s->at = b.id;
				s->first = false;
				s->edit = editing(b.words, true);
				s->edit.end(false);
				s->show_caret = s->jumped = true;
				if (!spec.pick.empty()) p.pick(spec.pick, s->at);
			}
			s->jump.clear();
		}
		// The outline: the title and headings, the section the caret is in marked.
		std::optional<desktop::view> outline;
		if (spec.outline) {
			std::vector<desktop::view> listed;
			auto label = t.quiet();
			label.size -= 2;
			label.weight = 600;
			listed.push_back(desktop::text("On this page", label).padding(edges(0, 0, 8, 12)));
			std::string section;
			for (const auto& b : blocks) {
				if (kind_of(b.kind).outlined() > 0) section = b.id;
				if (b.id == s->at) break;
			}
			for (const auto& b : blocks) {
				if (kind_of(b.kind).outlined() == 0 || b.words.empty()) continue;
				bool here = b.id == section;
				auto look = here ? t.body() : t.quiet();
				look.size = t.text_size - 1;
				if (kind_of(b.kind).outlined() == 1) look.weight = 600;
				auto go = [s, tick, id = b.id] {
					s->jump = id;
					tick.set(tick.peek() + 1);
				};
				listed.push_back(desktop::row(desktop::edge(here ? t.accent : t.line, 2), desktop::text(b.words, look).padding(edges(3, 0, 3, 10)).grow())
				                     .align(align::stretch)
				                     .on_click(std::move(go)));
			}
			if (listed.size() == 1) listed.push_back(desktop::text("Headings show here as you write them.", t.quiet()).padding(edges(0, 0, 0, 12)));
			outline = desktop::column(std::move(listed)).padding(edges(around_sheet, 16, 16, 0)).width(232);
		}
		auto made = std::make_unique<paper_element>(p, spec, s, std::move(blocks), tick, due);
		// What an insert button asked of it.
		if (auto asked = p.kept<std::optional<value>>("paper insert " + spec.key); asked.get()) {
			value given = *asked.peek();
			asked.set(std::nullopt);
			made->insert(given);
		}
		// Typing paused: what's been written is sent.
		if (due.get() != 0 && due.peek() == s->pauses) {
			due.set(0);
			made->send_all();
		}
		desktop::view shown(std::move(made));
		if (spec.fills) shown = desktop::scroll(std::move(shown)).grow();
		if (!outline) return shown;
		// Beside the page, and as much room on its other side, so the page stays in the
		// middle of the window, as it is without one.
		auto balance = desktop::column(std::vector<desktop::view>{}).width(232);
		return desktop::row(std::move(balance), std::move(shown).grow(), std::move(*outline)).align(align::stretch).grow();
	}

}  // namespace one::desktop::runtime
