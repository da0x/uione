// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Input methods, through text-input-v3: while a field has the keyboard, the
// compositor is told the text around its caret and where the caret is, and passes
// what an input method composes and types to the field.

#include <algorithm>
#include <cmath>

#include "app_internal.hpp"
#include "ui_internal.hpp"

namespace one::desktop {

	namespace {

		app::state* owning = nullptr;

		// The element typed into in the window with the keyboard, if any.
		text_entry* entry_in(app::state& a) {
			if (!a.input || !a.input->keyboard_in) return nullptr;
			auto& w = *a.input->keyboard_in;
			if (!w.root) return nullptr;
			element* focused = detail::focused_in(*w.root);
			return focused ? focused->entry() : nullptr;
		}

		const zwp_text_input_v3_listener listener{
		    .enter = [](void*, zwp_text_input_v3*, wl_surface*) {
			    owning->input->typing.entered = true;
			    owning->input->typing.told = {};  // tells it all again
		    },
		    .leave = [](void*, zwp_text_input_v3* handle, wl_surface*) {
			    auto& t = owning->input->typing;
			    t.entered = false;
			    if (t.enabled) {
				    zwp_text_input_v3_disable(handle);
				    zwp_text_input_v3_commit(handle);
				    ++t.commits;
				    t.enabled = false;
			    }
		    },
		    .preedit_string = [](void*, zwp_text_input_v3*, const char* text, std::int32_t begin, std::int32_t) {
			    auto& t = owning->input->typing;
			    t.pending_preedit = text ? text : "";
			    t.pending_caret = begin;
		    },
		    .commit_string = [](void*, zwp_text_input_v3*, const char* text) { owning->input->typing.pending_commit = text ? text : ""; },
		    .delete_surrounding_text = [](void*, zwp_text_input_v3*, std::uint32_t before, std::uint32_t after) {
			    auto& t = owning->input->typing;
			    t.pending_before = before;
			    t.pending_after = after;
		    },
		    // Applied all at once: what was erased, then what was typed, then what's composed.
		    .done = [](void*, zwp_text_input_v3*, std::uint32_t) {
			    auto& t = owning->input->typing;
			    if (text_entry* e = entry_in(*owning)) {
				    if (!t.pending_commit.empty() || t.pending_before || t.pending_after) e->commit(t.pending_commit, t.pending_before, t.pending_after);
				    e->compose(t.pending_preedit, t.pending_preedit.empty() ? -1 : t.pending_caret);
				    if (owning->input->keyboard_in) owning->input->keyboard_in->redraw();
			    }
			    t.pending_preedit.clear();
			    t.pending_commit.clear();
			    t.pending_before = t.pending_after = 0;
			    t.pending_caret = 0;
		    },
		    // Version 2's, which version 1, bound here, never sends.
		    .action = [](void*, zwp_text_input_v3*, std::uint32_t, std::uint32_t) {},
		    .language = [](void*, zwp_text_input_v3*, const char*) {},
		    .preedit_hint = [](void*, zwp_text_input_v3*, std::uint32_t, std::uint32_t, std::uint32_t) {},
		};

	}  // namespace

	void connect_text_input(app::state& a) {
		owning = &a;
		if (!a.text_inputs || !a.input || !a.input->handle || a.input->typing.handle) return;
		auto& t = a.input->typing;
		t.handle = zwp_text_input_manager_v3_get_text_input(a.text_inputs, a.input->handle);
		zwp_text_input_v3_add_listener(t.handle, &listener, nullptr);
	}

	void tell_input_method(app::state& a) {
		if (!a.input || !a.input->typing.handle) return;
		auto& t = a.input->typing;
		text_entry* e = t.entered ? entry_in(a) : nullptr;
		if (!e) {
			if (t.enabled) {
				zwp_text_input_v3_disable(t.handle);
				zwp_text_input_v3_commit(t.handle);
				++t.commits;
				t.enabled = false;
				t.told = {};
			}
			return;
		}
		// What it's told: the text near the caret, as much as the protocol carries.
		std::size_t caret = 0, anchor = 0;
		const std::string& text = e->surrounding(caret, anchor);
		std::size_t from = caret > 1500 ? caret - 1500 : 0, to = std::min(text.size(), caret + 1500);
		while (from > 0 && (static_cast<unsigned char>(text[from]) & 0xc0) == 0x80) --from;
		while (to < text.size() && (static_cast<unsigned char>(text[to]) & 0xc0) == 0x80) ++to;
		anchor = std::clamp(anchor, from, to);
		rect at = e->caret_area();
		typing_told now{e, text.substr(from, to - from), caret - from, anchor - from,
		                {static_cast<int>(std::lround(at.x)), static_cast<int>(std::lround(at.y)), static_cast<int>(std::lround(at.width)),
		                 static_cast<int>(std::lround(at.height))}};
		if (t.enabled && now == t.told) return;
		if (!t.enabled || now.entry != t.told.entry) zwp_text_input_v3_enable(t.handle);
		zwp_text_input_v3_set_surrounding_text(t.handle, now.text.c_str(), static_cast<std::int32_t>(now.caret), static_cast<std::int32_t>(now.anchor));
		zwp_text_input_v3_set_text_change_cause(t.handle, ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_OTHER);
		zwp_text_input_v3_set_content_type(t.handle, e->multiline() ? ZWP_TEXT_INPUT_V3_CONTENT_HINT_MULTILINE : ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE,
		                                   ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
		zwp_text_input_v3_set_cursor_rectangle(t.handle, now.area[0], now.area[1], now.area[2], now.area[3]);
		zwp_text_input_v3_commit(t.handle);
		++t.commits;
		t.enabled = true;
		t.told = std::move(now);
	}

}  // namespace one::desktop
