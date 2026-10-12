// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// A field: text typed into a box, edited by one::desktop::editing, drawn with its
// selection and caret, and kept in a state. It keeps its own copy as it's typed in,
// so typing doesn't build the view again; a value set elsewhere shows when it is.

#include <algorithm>
#include <cmath>

#include "one/desktop/editing.hpp"
#include "one/desktop/ui.hpp"
#include "ui_internal.hpp"

namespace one::desktop {

	namespace {

		class field_element : public element {
		public:
			field_element(state<std::string> value, field_options options)
			    : value_(std::move(value)), options_(std::move(options)), edit_(value_.peek(), options_.multiline) {
				padding = edges(7, 10);
			}

			bool focusable() const override { return true; }
			one::desktop::cursor cursor() const override { return cursor::text; }

			// What it was typing keeps, unless the value was changed elsewhere since.
			void adopt(element& before) override {
				element::adopt(before);
				auto& was = static_cast<field_element&>(before);
				edit_ = std::move(was.edit_);
				scroll_ = was.scroll_;
				edit_.replace_all(value_.peek());
			}

			extent measure_content(float width) override {
				const text_layout& laid = laid_out(width);
				float line = line_height();
				float height = options_.multiline ? std::max(laid.height(), line * static_cast<float>(options_.lines)) : line;
				return {options_.multiline ? width : std::min(width, 240.f), height};
			}

			void paint_content(canvas& c) override {
				const theme& t = current_theme();
				float radius = std::min(t.corners, 6.f);
				c.box(frame, {.fill = t.surface, .border = focused ? t.accent : t.line, .border_width = focused ? 1.5f : 1, .radius = radius});
				rect area = inner();
				auto clipped = c.clip(frame.inset(1));
				const text_layout& laid = laid_out(area.width);
				float scale = static_cast<float>(laid.scale());
				// One line keeps its caret in sight, scrolling across as it moves.
				if (!options_.multiline) {
					float caret_x = laid.caret(edit_.caret()).x / scale;
					if (caret_x - scroll_ > area.width - 2) scroll_ = caret_x - area.width + 2;
					if (caret_x < scroll_) scroll_ = caret_x;
					scroll_ = std::max(0.f, std::min(scroll_, std::max(0.f, laid.width() - area.width + 2)));
				}
				point origin{area.x - scroll_, area.y};
				if (edit_.text().empty()) {
					if (!options_.placeholder.empty()) c.text(lay_out({options_.placeholder, t.quiet()}, options_.multiline ? area.width : 0, laid.scale()), origin);
				} else {
					if (edit_.selecting()) {
						auto [from, to] = edit_.selection();
						color chosen = t.accent;
						chosen.alpha = focused ? 0.28f : 0.14f;
						auto start = laid.caret(from), finish = laid.caret(to);
						for (std::size_t l = start.line; l <= finish.line && l < laid.lines().size(); ++l) {
							const auto& line = laid.lines()[l];
							float left = l == start.line ? start.x : 0;
							float right = l == finish.line ? finish.x : std::max(line.width, left) + 6 * scale;
							c.box({origin.x + left / scale, origin.y + line.top / scale, (right - left) / scale, line.height / scale}, {.fill = chosen});
						}
					}
					c.text(laid, origin);
				}
				if (focused) {
					auto at = laid.caret(edit_.caret());
					const auto& line = laid.lines()[std::min(at.line, laid.lines().size() - 1)];
					c.box({origin.x + at.x / scale - 0.75f, origin.y + line.top / scale + 2, 1.5f, line.height / scale - 4}, {.fill = t.ink});
				}
			}

			bool handle_key(const key_event& e) override {
				bool word = e.control, extend = e.shift;
				if (e.is(keys::enter) || e.is(keys::keypad_enter)) {
					if (options_.multiline && !e.control) return typed("\n");
					if (!options_.on_submit) return false;  // for what it's in, like a form
					options_.on_submit();
					return true;
				}
				if (e.is(keys::backspace)) return changed([&] { edit_.erase_back(word); });
				if (e.is(keys::delete_key)) return changed([&] { edit_.erase_forward(word); });
				if (e.is(keys::left)) return moved([&] { edit_.left(word, extend); });
				if (e.is(keys::right)) return moved([&] { edit_.right(word, extend); });
				if (e.is(keys::home)) return moved([&] { e.control || !options_.multiline ? edit_.start(extend) : edit_.line_start(extend); });
				if (e.is(keys::end)) return moved([&] { e.control || !options_.multiline ? edit_.end(extend) : edit_.line_end(extend); });
				if ((e.is(keys::up) || e.is(keys::down)) && options_.multiline) return moved([&] { by_line(e.is(keys::up) ? -1 : 1, extend); });
				if (e.control && !e.alt) {
					switch (e.symbol) {
						case 'a': return moved([&] { edit_.select_all(); });
						case 'c':
							if (edit_.selecting()) detail::copy_text(edit_.selected());
							return true;
						case 'x':
							if (!edit_.selecting()) return true;
							detail::copy_text(edit_.selected());
							return changed([&] { edit_.erase_back(false); });
						case 'v':
							if (auto pasted = detail::pasted_text(); pasted && !pasted->empty()) return typed(*pasted);
							return true;
						case 'z': return changed([&] { edit_.undo(); });
						case 'Z':
						case 'y': return changed([&] { edit_.redo(); });
					}
					return false;
				}
				// What the key types, if it's text rather than a control character.
				if (!e.alt && !e.super && !e.text.empty() && static_cast<unsigned char>(e.text[0]) >= 0x20 && e.text[0] != 0x7f) return typed(e.text);
				return false;
			}

			bool press(point at, int clicks) override {
				std::size_t offset = offset_at(at);
				if (clicks == 2) edit_.select_word(offset);
				else if (clicks == 3) options_.multiline ? edit_.select_line(offset) : edit_.select_all();
				else edit_.move_to(offset, false);
				return true;
			}

			void drag(point at) override { edit_.move_to(offset_at(at), true); }

		private:
			float line_height() const {
				const text_style s = current_theme().body();
				return s.size * s.line_height;
			}

			const text_layout& laid_out(float width) {
				double scale = detail::layout_scale();
				float wrap = options_.multiline ? width : 0;
				if (!laid_ || laid_text_ != edit_.text() || wrap != width_ || scale != scale_) {
					laid_ = lay_out({edit_.text(), current_theme().body()}, wrap, scale);
					laid_text_ = edit_.text();
					width_ = wrap;
					scale_ = scale;
				}
				return *laid_;
			}

			std::size_t offset_at(point at) {
				rect area = inner();
				const text_layout& laid = laid_out(area.width);
				float scale = static_cast<float>(laid.scale());
				return laid.offset_at((at.x - area.x + scroll_) * scale, (at.y - area.y) * scale);
			}

			// A line up or down, as near the caret's x as there is.
			void by_line(int direction, bool extend) {
				const text_layout& laid = laid_out(inner().width);
				auto at = laid.caret(edit_.caret());
				if (direction < 0 && at.line == 0) return edit_.start(extend);
				if (direction > 0 && at.line + 1 >= laid.lines().size()) return edit_.end(extend);
				const auto& line = laid.lines()[at.line + direction];
				edit_.move_to(laid.offset_at(at.x, line.top + line.height / 2), extend);
			}

			template <class F>
			bool moved(F&& how) {
				how();
				keep_in_sight();
				return true;
			}

			template <class F>
			bool changed(F&& how) {
				std::string before = edit_.text();
				how();
				if (edit_.text() != before) value_.set(edit_.text());
				keep_in_sight();
				return true;
			}

			bool typed(std::string_view words) {
				return changed([&] { edit_.type(words); });
			}

			// The caret, within any scroll the field is in.
			void keep_in_sight() {
				rect area = inner();
				const text_layout& laid = laid_out(area.width);
				float scale = static_cast<float>(laid.scale());
				auto at = laid.caret(edit_.caret());
				const auto& line = laid.lines()[std::min(at.line, laid.lines().size() - 1)];
				reveal({area.x + at.x / scale, area.y + line.top / scale, 2, line.height / scale});
			}

			state<std::string> value_;
			field_options options_;
			editing edit_;
			float scroll_ = 0;  // across, for one line
			std::optional<text_layout> laid_;
			std::string laid_text_;
			float width_ = -1;
			double scale_ = 0;
		};

	}  // namespace

	view field(state<std::string> value, field_options options) {
		return view(std::make_unique<field_element>(std::move(value), std::move(options)));
	}

}  // namespace one::desktop
