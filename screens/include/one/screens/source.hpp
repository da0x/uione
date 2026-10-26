// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Where a native app's screens read their views and send their commands, as
// @uione/react's DataSource is for the web: an app's own backend through one::client,
// or what's kept in memory, for tests and for trying a screen out.
//
// Everything a source tells, it tells on the loop's thread, so a screen's states are
// set where they're read.

#pragma once

#include <ember.hpp>

#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace one::screens {

	using value = ember::value;

	// A view's document as a screen has it: arriving, there, not there or not
	// readable, or failed to arrive.
	enum class status { loading, ready, missing, failed };
	struct view_state {
		screens::status status = status::loading;
		value data = value::map();
		std::string error = "";
	};

	struct person {
		std::string uid, name;
	};

	// What a command answered: the id of what it changed, or why it was turned down.
	using answer = std::expected<std::string, std::string>;

	// Listening to a view until it's let go.
	class listening {
	public:
		listening() = default;
		explicit listening(std::function<void()> stop) : stop_(std::move(stop)) {}
		listening(listening&& other) noexcept : stop_(std::exchange(other.stop_, {})) {}
		listening& operator=(listening&& other) noexcept {
			if (this != &other) {
				if (stop_) stop_();
				stop_ = std::exchange(other.stop_, {});
			}
			return *this;
		}
		~listening() {
			if (stop_) stop_();
		}

	private:
		std::function<void()> stop_;
	};

	// Files on this machine for a command's file fields, by field, as written in a
	// form: paths, ~/ for home, or file:// addresses as a file manager copies them.
	using attached_files = std::map<std::string, std::vector<std::string>>;
	// Where a file written that way is.
	std::string local_path(std::string written);
	// A command's fields with a field holding where its files went: a list gets them
	// added to what it held, a single file is replaced by the first.
	value with_uploaded(value fields, const std::string& field, const std::vector<std::string>& uploaded);

	class source {
	public:
		virtual ~source() = default;
		// Listens to views/<view>, or views/<view>:<subject>, telling each state.
		virtual listening listen(const std::string& view, const std::string& subject, std::function<void(const view_state&)> told) = 0;
		// Runs a command, like projects::issue::move, telling what it answered.
		virtual void run(const std::string& command, const value& fields, std::function<void(const answer&)> answered) = 0;
		// Runs a command once the files given are uploaded, each field then holding where
		// its files went; a source that can't upload turns down any it's given.
		virtual void run(const std::string& command, const value& fields, const attached_files& files, std::function<void(const answer&)> answered);
		// Who's reading, when someone's signed in.
		virtual std::optional<person> reader() const { return std::nullopt; }
		// Told every second, on the loop, until it's stopped: for running times. A
		// source with no loop, like one kept in memory, never ticks.
		virtual listening ticking(std::function<void()>) { return {}; }
		// Where a file a field names, like files/<sha-256>, is on this machine, for what
		// draws it; a source that keeps none here says nowhere.
		virtual std::optional<std::filesystem::path> file_here(const std::string&) const { return std::nullopt; }
	};

	// Views and commands kept in memory: set a view and its listeners are told; a
	// command runs a function given for it, and every run is kept to be looked at.
	class memory_source : public source {
	public:
		using command = std::function<answer(const value& fields, memory_source& self)>;

		void set(const std::string& view, const std::string& subject, value data);
		void remove(const std::string& view, const std::string& subject);
		void on(const std::string& command_name, command handler) { commands_[command_name] = std::move(handler); }
		void sign_in(std::optional<person> who) { reader_ = std::move(who); }

		struct ran {
			std::string command;
			value fields;
		};
		const std::vector<ran>& runs() const { return runs_; }
		// The files uploaded, by where they were on this machine.
		const std::vector<std::string>& uploads() const { return uploads_; }

		listening listen(const std::string& view, const std::string& subject, std::function<void(const view_state&)> told) override;
		using source::run;
		void run(const std::string& command_name, const value& fields, std::function<void(const answer&)> answered) override;
		// Each file "uploaded" to uploads/ and its name, as long as it's there.
		void run(const std::string& command_name, const value& fields, const attached_files& files, std::function<void(const answer&)> answered) override;
		std::optional<person> reader() const override { return reader_; }

	private:
		static std::string key(const std::string& view, const std::string& subject) { return subject.empty() ? view : view + ":" + subject; }
		void tell(const std::string& at);

		std::map<std::string, view_state> views_;
		std::map<std::string, std::map<int, std::function<void(const view_state&)>>> listeners_;
		std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
		int next_ = 0;
		std::map<std::string, command> commands_;
		std::vector<ran> runs_;
		std::vector<std::string> uploads_;
		std::optional<person> reader_;
	};

}  // namespace one::screens
