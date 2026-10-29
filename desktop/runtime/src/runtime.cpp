// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The window: the bar across its top with the app's name, its navigation, its
// badges and who's reading, and the page shown, with its title and what goes
// around it.

#include "one/desktop/runtime/runtime.hpp"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <functional>
#include <iterator>
#include <mutex>
#include <optional>
#include <thread>

#include "one/desktop/notices.hpp"
#include "one/desktop/runtime/components.hpp"

namespace one::desktop::runtime {

	desktop::view page_view(navigator& nav, page& p, bool* alone) {
		const auto& t = current_theme();
		const auto& s = nav.site();
		const auto& shows = nav.shown(p);
		p.forget_keys();
		typing({});
		desktop::view built = shows.build ? shows.build(p) : desktop::column();
		auto around = p.take_around();
		bool wide = p.take_wide();
		// Filling the screen, or a page that fills the window, is only what it's made of.
		bool only = p.take_fill() || fullscreen();
		if (alone) *alone = only;
		if (only) return built;
		std::vector<desktop::view> parts;
		for (auto& v : around.above) parts.push_back(std::move(v));
		// A page called by the app's own name, like its front page, doesn't say it again under the bar.
		std::string title = p.title();
		if (!(title.empty() || title == s.name) || !around.beside.empty()) {
			std::vector<desktop::view> title_row;
			title_row.push_back(desktop::text(title == s.name ? "" : title, t.heading(26)).grow());
			for (auto& v : around.beside) title_row.push_back(std::move(v));
			parts.push_back(desktop::row(std::move(title_row)).spacing(10).align(align::center));
		}
		for (auto& v : around.under) parts.push_back(std::move(v));
		// A question asked before a command runs, answered here.
		auto asked = p.asked();
		if (const auto& asking = asked.get()) {
			parts.push_back(desktop::column(desktop::text(asking->question, t.strong()),
			                                desktop::row(desktop::button("Yes, go ahead", [&p] { p.answer_asked(true); }, true).autofocus(),
			                                             desktop::button("Cancel", [&p] { p.answer_asked(false); }))
			                                    .spacing(8))
			                    .spacing(10)
			                    .padding(16)
			                    .background({.fill = t.surface, .border = t.warning, .border_width = 1.5f, .radius = t.corners}));
		}
		if (std::string wrong = *p.problem(); !wrong.empty()) {
			auto style = t.strong();
			style.color = t.danger;
			parts.push_back(desktop::text(wrong, style));
		}
		parts.push_back(std::move(built));
		if (s.footer) {
			parts.push_back(desktop::divider());
			parts.push_back(s.footer(p));
		}
		if (wide) return desktop::scroll(desktop::column(std::move(parts)).spacing(16).padding(edges(24, 32)));
		return desktop::scroll(desktop::column(std::move(parts)).spacing(16).padding(edges(24, 32)).width(880)).align(align::center);
	}

	namespace {
		// What agents were working on when the window was asked to close, while it asks
		// whether to, which stops them.
		state<std::optional<std::vector<std::string>>> asking_to_close;
		std::function<void()> close_now;
		desktop::window* shown_in = nullptr;  // the window run shows the site in, while it runs
		const site* running = nullptr;        // and the site
		std::atomic<desktop::app*> posting_to = nullptr;  // the app it runs in, for work other threads hand back
		state<bool> dark_on{false};

		// The site's own look, light or dark, as the switch says.
		const desktop::theme& own_look() { return dark_on.peek() && running && running->dark ? *running->dark : running->look; }
	}  // namespace

	bool may_close(navigator& nav, bool stops_agents) {
		if (asking_to_close.peek()) {  // asked to close again: it does
			asking_to_close.set(std::nullopt);
			return true;
		}
		if (!stops_agents) return true;
		auto worked = nav.worked();
		if (worked.empty()) return true;
		asking_to_close.set(std::move(worked));
		return false;
	}

	namespace {
		std::function<bool(const key_event&)> typed_into;
	}  // namespace

	void typing(std::function<bool(const key_event&)> taking) { typed_into = std::move(taking); }

	bool key(navigator& nav, const key_event& e) {
		if (typed_into && typed_into(e)) return true;
		if (e.is(keys::escape) && asking_to_close.peek()) {
			asking_to_close.set(std::nullopt);
			return true;
		}
		if (e.is(keys::f11) && shown_in) {
			shown_in->fullscreen(!shown_in->fullscreen());
			return true;
		}
		// Esc leaves the screen filled before it goes back, as a browser's does.
		if (e.is(keys::escape) && shown_in && shown_in->fullscreen()) {
			shown_in->fullscreen(false);
			return true;
		}
		// What a line of the page is run by, like ctrl+z: the first said for a key.
		auto lines = nav.current().keys();
		for (const auto& [chord, run] : lines) {
			if (pressed(e, chord)) {
				run();
				return true;
			}
		}
		if (e.is(keys::escape) || e.is(keys::backspace)) return nav.back();
		return false;
	}

	std::optional<desktop::size> screen_size() { return shown_in ? shown_in->screen() : std::nullopt; }
	bool fullscreen() { return shown_in && shown_in->fullscreen(); }
	std::string app_name() { return running ? running->name : ""; }
	bool dark() { return dark_on.get(); }
	bool offers_dark() { return running && running->dark; }
	void dark(bool on) {
		if (!running || !running->dark) return;
		dark_on.set(on);
		screens::remembered::set("appearance", on ? "dark" : "light");
		if (shown_in) shown_in->look(own_look());
	}
	void fullscreen(bool on) {
		if (shown_in) shown_in->fullscreen(on);
	}

	desktop::view drawn(navigator& nav) {
		const auto& t = current_theme();
		const auto& s = nav.site();
		running = &s;
		(void)nav.changes();
		page& here = nav.current();
		// Keyed by where it is, so a page keeps nothing of the one before it.
		std::string key = std::format("{}#{}", here.address(), nav.depth());
		bool alone = false;
		desktop::view shown = page_view(nav, here, &alone).key(key).grow();
		if (alone) return shown;
		auto brand = t.strong();
		brand.color = t.accent;
		brand.size = 17;
		std::vector<desktop::view> bar;
		if (nav.depth() > 1) bar.push_back(desktop::button("‹ Back", [&nav] { nav.back(); }));
		bar.push_back(desktop::link(s.name, [&nav, &s] {
			if (!nav.open("/") && !s.screens.empty()) nav.open(s.screens.front().route);
		}, brand));
		for (const auto* listed : screens::in_navigation(s)) {
			const auto& screen = *listed;
			if (screen.nav.empty()) continue;
			std::string route = screen.route;
			bar.push_back(desktop::link(screen.nav, [&nav, route] { nav.open(route); }, screen.route == nav.shown(here).route ? t.strong() : t.body()));
		}
		for (const auto& b : s.badges) {
			auto counted = screens::show(field_of(nav.view(b.view, here.me())->data, b.field));
			if (counted.empty() || counted == "0") continue;
			auto style = t.strong();
			style.color = color::hex(0xffffff);
			style.size -= 2;
			bar.push_back(desktop::text(counted + " " + b.label, style).padding(edges(2, 8)).background({.fill = t.accent, .radius = 10}));
		}
		bar.push_back(desktop::spacer());
		if (s.dark) bar.push_back(desktop::link(dark() ? "Light" : "Dark", [] { dark(!dark_on.peek()); }, t.quiet()));
		if (auto who = here.reader(); who && !who->name.empty()) {
			bar.push_back(desktop::avatar(who->name, 24));
			bar.push_back(desktop::text(who->name, t.quiet()));
		}
		auto top = desktop::row(std::move(bar)).spacing(14).align(align::center).padding(edges(10, 20)).background({.fill = t.surface});
		std::vector<desktop::view> parts;
		parts.push_back(std::move(top));
		parts.push_back(desktop::divider());
		if (const auto& asking = asking_to_close.get()) {
			std::vector<desktop::view> said;
			said.push_back(desktop::text("Quit?", t.heading(20)));
			said.push_back(desktop::text(asking->size() == 1 ? "An agent is working on this, and quitting stops it midway:"
			                                                 : std::format("Agents are working on these {}, and quitting stops them midway:", asking->size()),
			                             t.strong()));
			for (const auto& worked : *asking) said.push_back(desktop::text("• " + worked, t.body()));
			said.push_back(desktop::row(desktop::button("Quit", [] { if (close_now) close_now(); }, true),
			                            desktop::button("Cancel", [] { asking_to_close.set(std::nullopt); }).autofocus())
			                   .spacing(8));
			parts.push_back(desktop::column(desktop::column(std::move(said))
			                                    .spacing(10)
			                                    .padding(16)
			                                    .background({.fill = t.surface, .border = t.warning, .border_width = 1.5f, .radius = t.corners}))
			                    .padding(edges(16, 32)));
		}
		parts.push_back(std::move(shown));
		return desktop::column(std::move(parts));
	}

	namespace {

		// Where a running copy of an app listens for addresses: a socket in this
		// person's runtime folder, named for the app.
		std::filesystem::path door_of(const std::string& site_id) {
			const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
			std::filesystem::path folder = runtime_dir && *runtime_dir ? std::filesystem::path(runtime_dir) : std::filesystem::temp_directory_path();
			return folder / (site_id + ".door");
		}

		bool address_of(const std::filesystem::path& door, sockaddr_un& at) {
			std::memset(&at, 0, sizeof at);
			at.sun_family = AF_UNIX;
			std::string where = door.string();
			if (where.size() >= sizeof at.sun_path) return false;
			std::memcpy(at.sun_path, where.c_str(), where.size() + 1);
			return true;
		}

		// The door a running app listens at, each address handed to it opened in the
		// current page's place on the app's own thread; closed, and its socket gone,
		// when the app stops.
		class door {
		public:
			door(const std::string& site_id, desktop::app& app, navigator& nav) : path_(door_of(site_id)) {
				sockaddr_un at;
				if (!address_of(path_, at)) return;
				listening_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
				if (listening_ < 0) return;
				// One left by a copy that stopped without closing it is taken over.
				std::filesystem::remove(path_);
				mode_t was = umask(0077);
				bool bound = bind(listening_, reinterpret_cast<sockaddr*>(&at), sizeof at) == 0 && listen(listening_, 4) == 0;
				umask(was);
				if (!bound) {
					::close(std::exchange(listening_, -1));
					return;
				}
				accepting_ = std::thread([this, &app, &nav] {
					for (;;) {
						int knocked = accept4(listening_, nullptr, nullptr, SOCK_CLOEXEC);
						if (knocked < 0) return;
						std::string address;
						char buffer[512];
						for (ssize_t got; address.size() < 4096 && (got = read(knocked, buffer, sizeof buffer)) > 0;) address.append(buffer, static_cast<std::size_t>(got));
						::close(knocked);
						while (!address.empty() && (address.back() == '\n' || address.back() == '\r')) address.pop_back();
						if (address.starts_with("/")) app.post([&nav, address] { nav.replace(address); });
					}
				});
			}
			~door() {
				if (listening_ < 0) return;
				shutdown(listening_, SHUT_RDWR);
				if (accepting_.joinable()) accepting_.join();
				::close(listening_);
				std::filesystem::remove(path_);
			}
			door(const door&) = delete;
			door& operator=(const door&) = delete;

		private:
			std::filesystem::path path_;
			int listening_ = -1;
			std::thread accepting_;
		};

	}  // namespace

	bool hand_over(const std::string& site_id, const std::string& address) {
		sockaddr_un at;
		if (!address_of(door_of(site_id), at)) return false;
		int knocking = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
		if (knocking < 0) return false;
		bool handed = connect(knocking, reinterpret_cast<sockaddr*>(&at), sizeof at) == 0;
		std::string line = address + "\n";
		if (handed) handed = write(knocking, line.data(), line.size()) == static_cast<ssize_t>(line.size());
		::close(knocking);
		return handed;
	}

	namespace {
		// What a test holds back of what's handed to the window, while it does.
		std::atomic<bool> held = false;
		std::mutex holding;
		std::vector<std::function<void()>> held_back;

		// The threads work away from the window's is done on, started the first time
		// there's some, each taking the next in turn. Never stopped: they wait, and go
		// with the app.
		class workers {
		public:
			void add(std::function<void()> work) {
				{
					std::lock_guard hold(lock_);
					if (started_ == 0) {
						started_ = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 1, 1, 4);
						for (int i = 0; i < started_; ++i) std::thread([this] { serve(); }).detach();
					}
					waiting_.push_back(std::move(work));
				}
				woken_.notify_one();
			}

		private:
			void serve() {
				for (;;) {
					std::function<void()> next;
					{
						std::unique_lock hold(lock_);
						woken_.wait(hold, [this] { return !waiting_.empty(); });
						next = std::move(waiting_.front());
						waiting_.pop_front();
					}
					next();
				}
			}
			std::mutex lock_;
			std::condition_variable woken_;
			std::deque<std::function<void()>> waiting_;
			int started_ = 0;
		};
		workers& shared_workers() {
			static workers* all = new workers();
			return *all;
		}
	}  // namespace

	void post(std::function<void()> task) {
		if (held) {
			std::lock_guard hold(holding);
			held_back.push_back(std::move(task));
			return;
		}
		if (desktop::app* app = posting_to.load()) app->post(std::move(task));
	}

	bool posting() { return held || posting_to.load() != nullptr; }

	void in_background(std::function<void()> work) {
		if (!posting()) return work();
		shared_workers().add(std::move(work));
	}

	void handed_back(std::function<void()> task) {
		if (posting()) post(std::move(task));
		else task();
	}

	void hold_posted(bool on) {
		held = on;
		if (!on) {
			std::lock_guard hold(holding);
			held_back.clear();
		}
	}

	std::size_t run_posted(std::size_t most) {
		std::vector<std::function<void()>> now;
		{
			std::lock_guard hold(holding);
			std::size_t taken = std::min(most, held_back.size());
			now.assign(std::make_move_iterator(held_back.begin()), std::make_move_iterator(held_back.begin() + static_cast<std::ptrdiff_t>(taken)));
			held_back.erase(held_back.begin(), held_back.begin() + static_cast<std::ptrdiff_t>(taken));
		}
		for (auto& task : now) task();
		return now.size();
	}

	namespace {
		// A site's notices, said by the desktop whatever screen is open (decision 0024):
		// what's due looked at once the views have come, and every quarter minute after,
		// each button pressed on one acting on its row back on the window's thread.
		class notices_said {
		public:
			notices_said(const site& s, screens::source& from) : name_(s.name), watch_(std::make_shared<screens::notice_watch>(from, s.notices)) {
				std::thread([watch = std::weak_ptr(watch_), on = on_, name = name_] {
					for (int waited = 0; *on; ++waited) {
						std::this_thread::sleep_for(std::chrono::seconds(1));
						if (waited % 15 != 2) continue;
						post([watch, name] {
							if (auto w = watch.lock()) look(w, name);
						});
					}
				}).detach();
			}
			~notices_said() { *on_ = false; }

		private:
			static void look(const std::shared_ptr<screens::notice_watch>& watch, const std::string& name) {
				for (auto& due : watch->due()) {
					std::vector<desktop::notice_action> actions;
					const auto& buttons = watch->specs()[due.spec].buttons;
					for (std::size_t i = 0; i < buttons.size(); ++i) actions.push_back({std::to_string(i), buttons[i].label});
					desktop::notify(name, due.title, due.body, std::move(actions), [weak = std::weak_ptr(watch), due](const std::string& key) {
						post([weak, due, key] {
							if (auto w = weak.lock()) w->press(due, static_cast<std::size_t>(std::stoul(key)));
						});
					});
				}
			}
			std::string name_;
			std::shared_ptr<screens::notice_watch> watch_;
			std::shared_ptr<std::atomic<bool>> on_ = std::make_shared<std::atomic<bool>>(true);
		};
	}  // namespace

	int run(const site& s, desktop::app& app, screens::source& from, const std::string& address) {
		posting_to = &app;
		screens::remembered::open(s.id);  // what this app remembers between runs, like how each board was shown
		auto& window = app.open({.title = s.name, .id = s.id, .size = {1200, 800}});
		running = &s;
		// Light or dark as it was last time, when its theme has both.
		dark_on.set(s.dark && screens::remembered::get("appearance") == "dark");
		window.look(own_look());
		// A theme tried from a palette draws the whole window, and the app's own again after.
		screens::tried_themes::on_tried([&window](const std::optional<palette::theme>& tried) {
			window.look(tried ? drawn_in(*tried, own_look()) : own_look());
		});
		navigator nav(s, from);
		if (!nav.open(address) && !nav.open(s.screens.empty() ? "/" : s.screens.front().route)) return 2;
		std::optional<notices_said> notices;
		if (!s.notices.empty()) notices.emplace(s, from);
		door handed_to(s.id, app, nav);
		shown_in = &window;
		window.show([&] {
			auto shown = drawn(nav);
			std::string title = nav.current().title();
			window.title(title.empty() || title == s.name ? s.name : title + " · " + s.name);
			return shown;
		});
		window.on_key = [&](const key_event& e) { return key(nav, e); };
		window.on_key_first = [](const key_event& e) { return typed_into && typed_into(e); };
		// Started with agents that stop when it quits, as publish starts its own, it asks
		// before closing while they're working on something.
		const char* stops = std::getenv("ONE_QUIT_STOPS_AGENTS");
		bool stops_agents = stops && *stops && std::string(stops) != "0";
		close_now = [&app] { app.quit(); };
		window.on_close = [&] {
			if (may_close(nav, stops_agents)) app.quit();
		};
		int ended = app.run();
		asking_to_close.set(std::nullopt);
		close_now = nullptr;
		shown_in = nullptr;
		running = nullptr;
		posting_to = nullptr;
		return ended;
	}

}  // namespace one::desktop::runtime
