// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A tracker's issues in a terminal: a list chosen with j and k, each opening a page
// of its own, and a thread that changes an issue every two seconds, as a live view
// would. What changed is shown in yellow for a few seconds.

#include <format>
#include <random>
#include <thread>

#include "one/terminal/terminal.hpp"

namespace terminal = one::terminal;

namespace {

	struct issue {
		std::string id;
		int number = 0;
		std::string title;
		std::string stage;
	};

	class issue_page : public terminal::document_screen {
	public:
		explicit issue_page(issue i) : issue_(std::move(i)) {
			loaded = true;
			rows.push_back({{std::format("#{}  ", issue_.number), terminal::dim()}, {issue_.title, terminal::bold()}});
			rows.push_back({});
			rows.push_back({{"Stage       ", terminal::dim()}, {issue_.stage, {}}});
		}
		std::string address() const override { return "/issues/" + std::to_string(issue_.number); }

	private:
		issue issue_;
	};

	class issue_list : public terminal::list_screen {
	public:
		issue_list() {
			static const char* titles[] = {"Title is already taken after renaming a phase", "Add emoji reactions to comments", "Ncurses CLI access",
			                               "Boards scroll sideways on a narrow screen", "Search finds words inside code blocks"};
			static const char* stages[] = {"Reported", "Backlog", "In progress", "Review", "Done"};
			for (int i = 0; i < 30; ++i) issues_.push_back({std::format("issue-{}", i + 1), i + 1, titles[i % 5], stages[i % 5]});
			loaded = true;
			build();
		}
		std::string address() const override { return "/issues"; }

		// A change from elsewhere, on the loop's thread.
		void changed(std::size_t which, int round) {
			auto& i = issues_[which % issues_.size()];
			i.title = std::format("Changed elsewhere ({})", round);
			build();
		}

	private:
		void build() {
			for (const auto& i : issues_) seen_.saw(i.id, i.title);
			seen_.settled();
			choices.clear();
			choices.push_back({terminal::plain("Issues", terminal::bold()), {}, true});
			for (const auto& i : issues_) {
				auto look = seen_.fresh(i.id) ? terminal::colored(terminal::color::yellow, true) : terminal::style{};
				choices.push_back({{{std::format("#{:<4} ", i.number), terminal::dim()}, {i.title, look}, {"   " + i.stage, terminal::dim()}},
				                   [i] { return std::make_unique<issue_page>(i); }});
			}
		}
		std::vector<issue> issues_;
		terminal::changes seen_;
	};

}  // namespace

int main() {
	terminal::app app({.name = "issues"});
	auto first = std::make_unique<issue_list>();
	auto* list = first.get();
	app.status = [](const terminal::screen&) { return terminal::line{{"● live", terminal::colored(terminal::color::green)}}; };
	std::jthread elsewhere([&](std::stop_token stopping) {
		std::mt19937 random(7);
		for (int round = 1; !stopping.stop_requested(); ++round) {
			for (int i = 0; i < 20 && !stopping.stop_requested(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
			std::size_t which = random();
			app.post([=] { list->changed(which, round); });
		}
	});
	return app.run(std::move(first));
}
