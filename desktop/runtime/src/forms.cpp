// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Forms, drawn: each field as its kind is, a field to type in, a box to tick, a
// picker or boxes for several, from what one::screens keeps of them and sends.

#include "one/desktop/runtime/components.hpp"

namespace one::desktop::runtime {

	namespace {

		text_style label_style() {
			auto s = current_theme().strong();
			s.size -= 1;
			return s;
		}

		std::vector<desktop::option> drawn_options(const std::vector<screens::option>& from) {
			std::vector<desktop::option> out;
			for (const auto& o : from) out.push_back({o.value, o.label});
			return out;
		}

	}  // namespace

	desktop::view form(page& p, form_spec spec) {
		form_model model(p, spec);
		if (!model.shown()) return {};
		const auto& t = current_theme();
		if (!model.open()) {
			std::string opens = model.opens();
			if (std::string g = glyph_of(spec.icon); !g.empty()) opens = g + " " + opens;
			return desktop::button(opens, [model] { model.set_open(true); }, true);
		}
		std::vector<desktop::view> parts;
		for (const auto& f : model.fields()) {
			std::string shown = model.shown_label(f);
			std::vector<desktop::view> field_parts;
			if (f.type != "boolean") field_parts.push_back(desktop::text(shown, label_style()));
			if (f.type == "boolean") {
				field_parts.push_back(desktop::toggle(model.ticked(f, "true"), shown));
			} else if (f.type == "choice" || f.type == "pick") {
				std::string lower = shown;
				if (!lower.empty() && lower[0] >= 'A' && lower[0] <= 'Z') lower[0] = static_cast<char>(lower[0] - 'A' + 'a');
				field_parts.push_back(desktop::picker(model.written(f), drawn_options(model.choices_of(f)), model.picking(f), "Choose " + lower + "…"));
			} else if (f.type == "choices") {
				std::vector<desktop::view> each;
				for (const auto& o : f.choices) each.push_back(desktop::toggle(model.ticked(f, o.value), o.label));
				field_parts.push_back(desktop::column(std::move(each)).spacing(4));
			} else {
				bool several = f.type == "markdown";
				std::string placeholder = f.type == "date" ? "YYYY-MM-DD" : f.type == "list" ? "Separated by commas" : "";
				field_parts.push_back(desktop::field(model.written(f), {.placeholder = placeholder, .multiline = several, .lines = several ? 6 : 1}));
			}
			if (!f.hint.empty()) field_parts.push_back(desktop::text(f.hint, t.quiet()));
			// A form opened from its button is typed into at once.
			if (spec.button && parts.empty() && field_parts.size() > 1) field_parts[1] = std::move(field_parts[1]).autofocus();
			parts.push_back(desktop::column(std::move(field_parts)).spacing(6));
		}
		if (std::string why = model.error(); !why.empty()) {
			auto s = t.body();
			s.color = t.danger;
			parts.push_back(desktop::text(why, s));
		}
		std::vector<desktop::view> buttons;
		buttons.push_back(desktop::button(model.busy() ? model.says() + "…" : model.says(), [model] { model.send(); }, true));
		if (spec.button) buttons.push_back(desktop::button("Cancel", [model] { model.set_open(false); }));
		parts.push_back(desktop::row(std::move(buttons)).spacing(8));
		auto panel = desktop::column(std::move(parts)).spacing(12).padding(16).background({.fill = t.surface, .border = t.line, .border_width = 1, .radius = t.corners});
		// Opened from its button, it goes under the title, across the page, in its button's place.
		if (spec.button) {
			p.under_title(std::move(panel));
			return {};
		}
		return panel;
	}

}  // namespace one::desktop::runtime
