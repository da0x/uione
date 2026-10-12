// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Markdown as views: one::markdown's tree of blocks, each laid out by one::desktop,
// its spans in the theme's styles.

#include "one/desktop/runtime/markdown.hpp"

#include <one/markdown.hpp>

#include <algorithm>
#include <format>

namespace one::desktop::runtime {

	namespace {

		namespace md = one::markdown;

		void spans_of(const std::vector<md::inline_node>& nodes, text_style style, std::vector<text_span>& out) {
			const auto& t = current_theme();
			for (const auto& node : nodes) {
				text_style s = style;
				switch (node.kind) {
					case md::inline_kind::text: {
						// A line break within a paragraph is a space.
						std::string words = node.text;
						std::ranges::replace(words, '\n', ' ');
						out.push_back({std::move(words), s});
						break;
					}
					case md::inline_kind::code:
						s.family = t.code_family;
						s.size = style.size - 1;
						s.color = t.danger;
						out.push_back({node.text, s});
						break;
					case md::inline_kind::emphasis:
						s.italic = true;
						spans_of(node.children, s, out);
						break;
					case md::inline_kind::strong:
						s.weight = 700;
						spans_of(node.children, s, out);
						break;
					case md::inline_kind::strong_emphasis:
						s.weight = 700;
						s.italic = true;
						spans_of(node.children, s, out);
						break;
					case md::inline_kind::strikethrough:
						s.strike = true;
						spans_of(node.children, s, out);
						break;
					case md::inline_kind::link:
						s.color = t.accent;
						s.underline = true;
						spans_of(node.children, s, out);
						break;
					case md::inline_kind::image: {
						s.color = t.muted;
						std::vector<text_span> alt;
						spans_of(node.children, s, alt);
						std::string words;
						for (const auto& a : alt) words += a.text;
						out.push_back({"[" + (words.empty() ? std::string("image") : words) + "]", s});
						break;
					}
				}
			}
		}

		desktop::view paragraph(const std::vector<md::inline_node>& nodes, text_style style) {
			std::vector<text_span> spans;
			spans_of(nodes, style, spans);
			if (spans.empty()) spans.push_back({"", style});
			return desktop::text(std::move(spans));
		}

		desktop::view block(const md::block_node& b);

		desktop::view blocks(const std::vector<md::block_node>& children, float spacing = 12) {
			std::vector<desktop::view> shown;
			for (const auto& child : children) shown.push_back(block(child));
			return desktop::column(std::move(shown)).spacing(spacing);
		}

		desktop::view block(const md::block_node& b) {
			const auto& t = current_theme();
			switch (b.kind) {
				case md::block_kind::document: return blocks(b.children);
				case md::block_kind::heading: {
					static constexpr float sizes[] = {24, 20, 17, 15, 15, 15};
					return paragraph(b.inlines, t.heading(sizes[std::clamp(b.level, 1, 6) - 1]));
				}
				case md::block_kind::paragraph: return paragraph(b.inlines, t.body());
				case md::block_kind::code_block: {
					std::string code = b.literal;
					while (!code.empty() && code.back() == '\n') code.pop_back();
					return desktop::text(code, t.code()).padding(edges(10, 14)).background({.fill = t.sunken, .radius = 6});
				}
				case md::block_kind::block_quote: return desktop::row(desktop::edge(t.line), blocks(b.children).grow()).spacing(12).align(align::stretch);
				case md::block_kind::list: {
					std::vector<desktop::view> items;
					int n = b.start;
					for (const auto& item : b.children) {
						std::string marker = b.ordered ? std::format("{}.", n++) : "•";
						if (item.task_state == 0) marker = "☐";
						if (item.task_state == 1) marker = "☑";
						text_style look = t.body();
						look.color = t.muted;
						items.push_back(desktop::row(desktop::text(marker, look).width(22), blocks(item.children, 6).grow()).align(align::start));
					}
					return desktop::column(std::move(items)).spacing(4);
				}
				case md::block_kind::list_item: return blocks(b.children, 6);
				case md::block_kind::thematic_break: return desktop::divider();
				case md::block_kind::table: {
					std::vector<desktop::view> rows;
					for (std::size_t r = 0; r < b.rows.size(); ++r) {
						std::vector<desktop::view> cells;
						for (const auto& cell : b.rows[r]) cells.push_back(paragraph(cell, r == 0 ? t.strong() : t.body()).grow());
						rows.push_back(desktop::row(std::move(cells)).spacing(16).align(align::start));
						if (r == 0) rows.push_back(desktop::divider());
					}
					return desktop::column(std::move(rows)).spacing(6);
				}
			}
			return desktop::text("");
		}

	}  // namespace

	desktop::view markdown_view(const std::string& source) { return blocks(md::parse(source)); }

	desktop::view pages(page& p, std::string base, std::vector<written_page> all) {
		if (all.empty()) return {};
		const auto& t = current_theme();
		std::string here = p.param("page");
		const written_page* shown = &all.front();
		for (const auto& w : all) {
			if (w.name == here) shown = &w;
		}
		std::vector<desktop::view> side;
		for (const auto& w : all) {
			if (&w == shown) {
				auto s = t.strong();
				s.color = t.accent;
				side.push_back(desktop::text(w.title, s).padding(edges(5, 10)).background({.fill = t.sunken, .radius = std::min(t.corners, 6.f)}));
			} else {
				side.push_back(desktop::link(w.title, [&p, to = base + "/" + w.name] { p.open(to); }, t.body()).padding(edges(5, 10)));
			}
		}
		return desktop::row(desktop::column(std::move(side)).spacing(2).width(200), markdown_view(shown->source).grow()).spacing(28).align(align::start);
	}

	desktop::view markdown(page&, state<view_state> from, std::string field, std::string empty) {
		const view_state& v = *from;
		if (v.status != status::ready) return {};
		std::string source = field_of(v.data, field).as_string();
		if (source.empty()) return empty.empty() ? desktop::view() : desktop::text(empty, current_theme().quiet());
		return markdown_view(source);
	}

}  // namespace one::desktop::runtime
