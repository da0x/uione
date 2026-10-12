// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// A tracker's issues in a window: a list chosen with the keys or the mouse, the
// chosen one's details beside it, buttons that add issues and turn the order around,
// and a thread that changes an issue every two seconds, as a live view would. The
// chosen row and the scroll stay put through all of it.

#include <algorithm>
#include <atomic>
#include <format>
#include <print>
#include <random>
#include <thread>

#include "one/desktop/app.hpp"
#include "one/desktop/state.hpp"
#include "one/desktop/ui.hpp"

namespace {

	struct issue {
		std::string id;
		int number = 0;
		std::string title;
		std::string stage;
		int urgency = 2;  // 0 critical … 4 trivial
		std::string reporter;
	};

	one::desktop::color urgency_color(int urgency) {
		static constexpr std::uint32_t tones[] = {0xc2312d, 0xd4700b, 0x2457d6, 0x8a94a6, 0xc5ccd6};
		return one::desktop::color::hex(tones[std::clamp(urgency, 0, 4)]);
	}

	std::vector<issue> made_up(int count) {
		static const char* titles[] = {"Title is already taken after renaming a phase", "Add emoji reactions to comments", "Ncurses CLI access",
		                               "Boards scroll sideways on a narrow screen", "A milestone's issues sorted by urgency",
		                               "Search finds words inside code blocks", "Mentions notify people who follow the board"};
		static const char* stages[] = {"Reported", "Backlog", "In progress", "Review", "Done"};
		static const char* people[] = {"Maya Garcia", "Daher Alfawares", "Lin Wei", "Sam Okafor", "Ana Lima"};
		std::vector<issue> out;
		for (int i = 0; i < count; ++i) {
			out.push_back({std::format("demo-{}", i + 1), i + 1, titles[i % std::size(titles)], stages[i % std::size(stages)], i % 5, people[i % std::size(people)]});
		}
		return out;
	}

}  // namespace

int main() {
	auto app = one::desktop::app::connect();
	if (!app) {
		std::println(stderr, "demo: {}", app.error());
		return 1;
	}
	auto& window = app->open({.title = "Issues", .id = "org.uione.demo", .size = {980, 640}});

	one::desktop::state<std::vector<issue>> issues = made_up(40);
	one::desktop::state<std::string> opened = std::string("demo-1");
	one::desktop::state<std::string> changed;  // the issue the thread changed last

	window.show([=]() mutable {
		const auto& t = one::desktop::current_theme();
		const auto& all = *issues;
		auto shown = std::ranges::find(all, *opened, &issue::id);

		auto add = [=]() mutable {
			issues.change([](auto& list) {
				int number = static_cast<int>(list.size()) + 1;
				list.insert(list.begin(), {std::format("demo-{}", number), number, "A new issue, added at the top", "Reported", 1, "Lin Wei"});
			});
		};
		auto reverse = [=]() mutable { issues.change([](auto& list) { std::ranges::reverse(list); }); };

		auto rows = one::desktop::list(
		    all, &issue::id,
		    [&](const issue& i) {
			    one::desktop::text_style title = t.body();
			    if (i.id == *changed) title.color = t.accent;
			    return one::desktop::row(one::desktop::edge(urgency_color(i.urgency)), one::desktop::text(std::format("#{}", i.number), t.quiet()).width(40),
			                     one::desktop::text(i.title, title).grow(), one::desktop::text(i.stage, t.quiet()))
			        .spacing(10);
		    },
		    [=](const std::string& id) mutable { opened.set(id); });

		one::desktop::view details = one::desktop::text("Choose an issue.", t.quiet());
		if (shown != all.end()) {
			details = one::desktop::column(one::desktop::text(std::format("#{}", shown->number), t.quiet()), one::desktop::text(shown->title, t.heading()),
			                       one::desktop::row(one::desktop::avatar(shown->reporter), one::desktop::text(shown->reporter + " reported it", t.quiet())).spacing(8).align(one::desktop::align::center),
			                       one::desktop::divider(),
			                       one::desktop::row(one::desktop::text("Stage", t.quiet()).width(90), one::desktop::text(shown->stage)),
			                       one::desktop::row(one::desktop::text("Urgency", t.quiet()).width(90), one::desktop::edge(urgency_color(shown->urgency)),
			                                 one::desktop::text(std::format("{} of 5", shown->urgency + 1)))
			                           .spacing(6),
			                       one::desktop::text("Each change made elsewhere arrives here as it happens, and the row you've chosen stays chosen."))
			              .spacing(12);
		}

		return one::desktop::column(one::desktop::row(one::desktop::text("Issues", t.heading()), one::desktop::spacer(), one::desktop::button("Turn around", reverse),
		                              one::desktop::button("Add an issue", add, true))
		                        .spacing(10)
		                        .align(one::desktop::align::center),
		                    one::desktop::row(one::desktop::scroll(std::move(rows))
		                                  .grow(3)
		                                  .background({.fill = t.surface, .border = t.line, .border_width = 1, .radius = t.corners}),
		                              std::move(details).padding(20).grow(2).background({.fill = t.surface, .border = t.line, .border_width = 1, .radius = t.corners}))
		                        .spacing(16)
		                        .align(one::desktop::align::stretch)
		                        .grow())
		    .spacing(16)
		    .padding(24);
	});
	window.on_key = [&](const one::desktop::key_event& e) {
		if (e.is(one::desktop::keys::escape) || e.is('q')) {
			app->quit();
			return true;
		}
		return false;
	};

	// Changes from elsewhere, as a listener on another thread would hear them.
	std::atomic<bool> running = true;
	std::thread elsewhere([&, issues, changed]() mutable {
		std::mt19937 random(7);
		int round = 0;
		while (running) {
			for (int i = 0; i < 20 && running; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
			if (!running) break;
			++round;
			std::size_t which = random() % 40;
			app->post([=]() mutable {
				std::string id;
				issues.change([&](auto& list) {
					if (list.empty()) return;
					auto& i = list[which % list.size()];
					i.title = std::format("Changed elsewhere ({})", round);
					i.urgency = (i.urgency + 1) % 5;
					id = i.id;
				});
				changed.set(id);
			});
		}
	});
	int code = app->run();
	running = false;
	elsewhere.join();
	return code;
}
