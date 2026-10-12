// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Threads and timelines, drawn: what people wrote, one after another, and what
// happened to something, each change in the words one::screens says it with.

#include "one/desktop/runtime/components.hpp"

namespace one::desktop::runtime {

	desktop::view thread(page&, thread_spec spec) {
		const view_state& v = *spec.view;
		const auto& t = current_theme();
		if (v.status == status::loading) return desktop::text("Loading…", t.quiet());
		std::vector<desktop::view> entries;
		for (const auto& e : thread_of(v, spec.list)) {
			entries.push_back(desktop::row(desktop::avatar(e.author, 28),
			                               desktop::column(desktop::row(desktop::text(e.author, t.strong()), desktop::text(e.when, t.quiet())).spacing(8),
			                                               spec.body ? spec.body(e.body) : desktop::text(e.body, t.body()))
			                                   .spacing(6)
			                                   .grow())
			                      .spacing(12)
			                      .align(align::start)
			                      .padding(14)
			                      .background({.fill = t.surface, .border = t.line, .border_width = 1, .radius = t.corners}));
		}
		if (entries.empty()) return desktop::text("Nothing yet.", t.quiet());
		return desktop::column(std::move(entries)).spacing(10);
	}

	desktop::view timeline(page& p, timeline_spec spec) {
		const auto& t = current_theme();
		auto model = timeline_of(p, spec);
		if (model.entries.empty()) return {};  // shown only while it holds something
		std::vector<desktop::view> parts;
		if (!model.title.empty()) {
			auto heading_style = t.strong();
			heading_style.size += 2;
			parts.push_back(desktop::text(model.title, heading_style));
		}
		for (const auto& e : model.entries) {
			// What was done is one sentence, wrapped word by word where it's long, what
			// it's about in the accent color when it opens; when it was goes at the side.
			std::vector<text_span> said{{e.who, t.strong()}, {" " + e.what, t.body()}};
			if (!e.subject.empty()) {
				auto about = t.strong();
				if (!e.address.empty()) about.color = t.accent;
				said.push_back({" " + e.subject, about});
			}
			if (!e.after.empty()) said.push_back({" " + e.after, t.body()});
			std::vector<desktop::view> entry;
			entry.push_back(desktop::edge(e.fresh ? t.accent : t.line));
			entry.push_back(desktop::avatar(e.who, 22));
			entry.push_back(desktop::text(std::move(said)).grow());
			entry.push_back(desktop::text(e.when, t.quiet()));
			auto shown = desktop::row(std::move(entry)).spacing(10).align(align::center).padding(edges(4, 0));
			if (!e.address.empty()) shown = std::move(shown).on_click([&p, to = e.address] { p.open(to); });
			parts.push_back(std::move(shown));
		}
		return desktop::column(std::move(parts)).spacing(4);
	}

}  // namespace one::desktop::runtime
