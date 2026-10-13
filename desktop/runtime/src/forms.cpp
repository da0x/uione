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
			// A button with an icon is a quiet one, as on the web, beside the page's main ones.
			if (std::string g = glyph_of(spec.icon); !g.empty()) opens = g + " " + opens;
			// As wide as it says, as on the web, not across the page, and no wider among
			// the buttons beside a title, so they stay on one line beside it.
			return desktop::column(desktop::button(opens, [model] { model.set_open(true); }, spec.icon.empty())).align(align::start);
		}
		std::vector<desktop::view> parts;
		for (const auto& f : model.fields()) {
			std::string shown = model.shown_label(f);
			std::vector<desktop::view> field_parts;
			if (f.type != "boolean") field_parts.push_back(desktop::text(shown, label_style()));
			if (f.type == "boolean") {
				field_parts.push_back(desktop::toggle(model.ticked(f, "true"), shown));
			} else if (form_model::few(f)) {
				field_parts.push_back(desktop::radio_buttons(model.written(f), drawn_options(f.choices)));
			} else if (f.type == "slider") {
				field_parts.push_back(desktop::slider(model.written(f), {.from = f.range.from, .to = f.range.to, .by = f.range.by}));
			} else if (f.type == "choice" || f.type == "pick") {
				std::string lower = shown;
				if (!lower.empty() && lower[0] >= 'A' && lower[0] <= 'Z') lower[0] = static_cast<char>(lower[0] - 'A' + 'a');
				field_parts.push_back(desktop::picker(model.written(f), drawn_options(model.choices_of(f)), model.picking(f), "Choose " + lower + "…"));
			} else if (f.type == "file" || f.type == "files") {
				// Where the files are on this machine, typed or pasted from a file manager,
				// uploaded as the form is sent.
				bool several = f.type == "files";
				field_parts.push_back(desktop::field(model.written(f), {.placeholder = several ? "Files to attach, one a line, like ~/screenshot.png" : "A file to attach, like ~/screenshot.png",
				                                                        .multiline = several,
				                                                        .lines = several ? 2 : 1}));
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
