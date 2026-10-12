// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// Views: what a window shows, said as C++ values and built again whenever a state they
// read changes:
//
//     window.show([=] {
//         return loom::column(
//             loom::text(page->title, theme.heading()),
//             loom::scroll(loom::list(page->issues, &issue::id, [](const issue& i) {
//                 return loom::row(loom::edge(urgency(i)), loom::text(i.title)).spacing(8);
//             }, [](const std::string& id) { open(id); }))
//         ).spacing(12).padding(24);
//     });
//
// What's built is matched with what was there before, by position and kind, or by
// key where one is given, so a list's chosen row, a scroll's offset and focus stay
// where they were when the view changes underneath them.

#pragma once

#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include "loom/canvas.hpp"
#include "loom/input.hpp"
#include "loom/text.hpp"
#include "loom/theme.hpp"

namespace loom {

	// Space inside a view's edge.
	struct edges {
		float top = 0, right = 0, bottom = 0, left = 0;
		edges() = default;
		edges(float all) : top(all), right(all), bottom(all), left(all) {}
		edges(float vertical, float horizontal) : top(vertical), right(horizontal), bottom(vertical), left(horizontal) {}
		edges(float t, float r, float b, float l) : top(t), right(r), bottom(b), left(l) {}
	};

	// A size in logical pixels, fractional.
	struct extent {
		float width = 0, height = 0;
	};

	enum class align { start, center, end, stretch };

	enum class cursor { normal, pointer, text };

	// An element of a built view. Most programs use the functions below instead of
	// these; a new kind of element derives from it.
	class element {
	public:
		virtual ~element() = default;

		// What every element can be given.
		std::string key;
		edges padding;
		std::optional<float> fixed_width, fixed_height;
		float grow = 0;         // a share of what's left in a row or column
		float spacing = 0;      // between children, in rows and columns
		align cross = align::stretch;  // children across a row or column
		bool fills_across = false;     // takes a row's whole height, or a column's width, however its siblings align
		bool wants_focus = false;      // has the keyboard when the view is first shown
		box_style background;
		std::function<void()> on_click;
		std::function<bool(const key_event&)> on_key;

		std::vector<std::unique_ptr<element>> children;
		element* parent = nullptr;
		rect frame;  // where it was placed, in the window

		// Layout: the size wanted within a width, then the place given.
		extent measure(float width);
		void place(const rect& area);
		void paint(canvas& c);

		// Input, each saying whether it was handled.
		virtual bool handle_key(const key_event& e);
		virtual bool click(point at);
		virtual bool wheel(point at, float dx, float dy);
		virtual element* hit(point at);
		virtual bool focusable() const { return false; }
		virtual bool prefers_focus() const { return false; }  // given the keyboard first, as a list is
		virtual loom::cursor cursor() const { return on_click ? cursor::pointer : cursor::normal; }

		// Takes over what a view kept from the element it replaces.
		virtual void adopt(element& before);

		bool focused = false;
		bool hovered = false;

		// Brings an area of this element into view, through any scroll above it.
		void reveal(const rect& area);

	protected:
		virtual extent measure_content(float width);
		virtual void place_content(const rect& inner);
		virtual void paint_content(canvas& c);
		virtual bool reveal_here(const rect&) { return false; }
		rect inner() const;
	};

	// A view as it's being said: one element, given things in turn.
	class view {
	public:
		view() = default;
		template <std::derived_from<element> E>
		view(std::unique_ptr<E> e) : element_(std::move(e)) {}

		view&& padding(edges e) && { element_->padding = e; return std::move(*this); }
		view&& width(float w) && { element_->fixed_width = w; return std::move(*this); }
		view&& height(float h) && { element_->fixed_height = h; return std::move(*this); }
		view&& grow(float share = 1) && { element_->grow = share; return std::move(*this); }
		view&& spacing(float s) && { element_->spacing = s; return std::move(*this); }
		view&& align(loom::align a) && { element_->cross = a; return std::move(*this); }
		view&& background(box_style s) && { element_->background = s; return std::move(*this); }
		view&& key(std::string k) && { element_->key = std::move(k); return std::move(*this); }
		view&& on_click(std::function<void()> f) && { element_->on_click = std::move(f); return std::move(*this); }
		view&& on_key(std::function<bool(const key_event&)> f) && { element_->on_key = std::move(f); return std::move(*this); }
		view&& autofocus() && { element_->wants_focus = true; return std::move(*this); }

		std::unique_ptr<element> take() && { return std::move(element_); }
		element* get() const { return element_.get(); }

	private:
		std::unique_ptr<element> element_;
	};

	// Text, wrapped to the width it's given.
	view text(std::string words, std::optional<text_style> style = {});
	view text(std::vector<text_span> spans);

	// Children one under another, or side by side.
	view column(std::vector<view> children);
	view row(std::vector<view> children);
	template <std::convertible_to<view>... V>
	view column(V&&... children) {
		std::vector<view> all;
		(all.push_back(view(std::forward<V>(children))), ...);
		return column(std::move(all));
	}
	template <std::convertible_to<view>... V>
	view row(V&&... children) {
		std::vector<view> all;
		(all.push_back(view(std::forward<V>(children))), ...);
		return row(std::move(all));
	}

	// One view scrolled within the space given, down, or across.
	view scroll(view content);
	view scroll_across(view content);

	// Rows, one of them chosen: moved with the arrows, j and k, Home, End and the page
	// keys, opened with Enter or a click. Each row has a key, so the chosen one stays
	// chosen as rows come and go.
	struct list_row {
		std::string key;
		view shown;
	};
	view list(std::vector<list_row> rows, std::function<void(const std::string&)> on_open = {});
	template <std::ranges::input_range R, class Key, class Show>
	view list(const R& items, Key key_of, Show show, std::function<void(const std::string&)> on_open = {}) {
		std::vector<list_row> rows;
		for (const auto& item : items) rows.push_back({std::string(std::invoke(key_of, item)), view(std::invoke(show, item))});
		return list(std::move(rows), std::move(on_open));
	}

	view button(std::string label, std::function<void()> pressed, bool primary = false);
	view edge(color c, float width = 3);      // a bar down a row's side, like how urgent it is
	view avatar(const std::string& name, float size = 24);  // a person's first letter in a color of theirs
	view divider();
	view spacer();  // takes what's left in a row or column

}  // namespace loom
