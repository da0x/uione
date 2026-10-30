// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Markdown as views: one::markdown's tree of blocks, each laid out by one::desktop,
// its spans in the theme's styles.

#include "one/desktop/runtime/markdown.hpp"

#include "one/desktop/runtime/components.hpp"

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
					case md::inline_kind::math:
						out.push_back({"$" + node.text + "$", s});
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

		// One view in a column, which can be centered in it.
		desktop::view alone(desktop::view v) {
			std::vector<desktop::view> one;
			one.push_back(std::move(v));
			return desktop::column(std::move(one));
		}

		desktop::view paragraph(const std::vector<md::inline_node>& nodes, text_style style) {
			// An equation alone is set on a line of its own, centered.
			if (nodes.size() == 1 && nodes.front().kind == md::inline_kind::math) return alone(equation(nodes.front().text, true)).align(align::center);
			bool math = std::ranges::any_of(nodes, [](const auto& n) { return n.kind == md::inline_kind::math; });
			std::vector<text_span> spans;
			if (!math) {
				spans_of(nodes, style, spans);
				if (spans.empty()) spans.push_back({"", style});
				return desktop::text(std::move(spans));
			}
			// With equations among its words, each word is its own piece, so the line
			// can break between them, and every piece sits on the words' baseline. The
			// space after a word is kept as one that doesn't break, which isn't trimmed.
			std::vector<desktop::view> pieces;
			for (const auto& node : nodes) {
				if (node.kind == md::inline_kind::math) {
					pieces.push_back(equation(node.text, false));
					continue;
				}
				std::vector<text_span> some;
				spans_of({node}, style, some);
				for (const auto& span : some) {
					std::size_t at = 0;
					while (at < span.text.size()) {
						std::size_t end = span.text.find(' ', at);
						std::string word = span.text.substr(at, (end == std::string::npos ? span.text.size() : end) - at);
						if (end != std::string::npos) word += "\u00A0";
						end = end == std::string::npos ? span.text.size() : end + 1;
						pieces.push_back(desktop::text(std::vector<text_span>{{std::move(word), span.style}}));
						at = end;
					}
				}
			}
			return desktop::flow(std::move(pieces));
		}

		desktop::view block(const md::block_node& b);

		// Whether what's being drawn is set as a document: its book face, its headings centered.
		bool documented = false;

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
					text_style look = t.heading(sizes[std::clamp(b.level, 1, 6) - 1]);
					if (!documented) return paragraph(b.inlines, look);
					look.family = t.document_family;
					if (b.level <= 2) look.weight = 400;
					return alone(paragraph(b.inlines, look)).align(align::center).padding(edges(b.level <= 2 ? 14 : 8, 0, 2, 0));
				}
				case md::block_kind::paragraph: {
					text_style look = t.body();
					if (documented) look.family = t.document_family;
					return paragraph(b.inlines, look);
				}
				case md::block_kind::code_block: {
					std::string code = b.literal;
					while (!code.empty() && code.back() == '\n') code.pop_back();
					// A math block set as an equation; a chem block as a chemical one, as ce( ) reads it.
					if (b.info == "math" || b.info == "chem") return alone(equation(b.info == "chem" ? "ce(" + code + ")" : code, true)).align(align::center).padding(edges(4, 0));
					// A diagram block drawn as its blocks and wires.
					if (b.info == "diagram") return alone(diagram(code)).align(align::center).padding(edges(4, 0));
					return desktop::text(colored_code(code, b.info, t.code(), t)).padding(edges(10, 14)).background({.fill = t.sunken, .radius = 6});
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
				case md::block_kind::thematic_break:
					if (documented) return alone(desktop::text("❦", t.quiet())).align(align::center).padding(edges(10, 0));
					return desktop::divider();
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

	desktop::view document_view(const std::string& source) {
		documented = true;
		desktop::view drawn = markdown_view(source);
		documented = false;
		const auto& t = current_theme();
		return alone(std::move(drawn)).padding(edges(36, 44)).background({.fill = t.surface, .border = t.line, .border_width = 1, .radius = t.corners});
	}

	desktop::view pages(page& p, std::string base, std::vector<written_page> all, bool document) {
		if (all.empty()) return {};
		const auto& t = current_theme();
		std::string here = p.param("page");
		const written_page* shown = &all.front();
		for (const auto& w : all) {
			if (w.name == here) shown = &w;
		}
		// One page is just the page, with no list of one beside it.
		auto drawn = [&] { return document ? document_view(shown->source) : markdown_view(shown->source); };
		if (all.size() == 1) return drawn();
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
		return desktop::row(desktop::column(std::move(side)).spacing(2).width(200), drawn().grow()).spacing(28).align(align::start);
	}

	desktop::view markdown(page&, state<view_state> from, std::string field, std::string empty) {
		const view_state& v = *from;
		if (v.status != status::ready) return {};
		std::string source = field_of(v.data, field).as_string();
		if (source.empty()) return empty.empty() ? desktop::view() : desktop::text(empty, current_theme().quiet());
		return markdown_view(source);
	}

}  // namespace one::desktop::runtime
