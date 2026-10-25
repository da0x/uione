// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A project that says storage local keeps everything on the machine it runs on
// (decision 0015): each command's event appended to a log in its folder, each file
// it's given kept once by its SHA-256, and its views built from the entities the
// events have left, told live to the screens through the same one::screens::source
// they'd read from the cloud. The commands and views themselves are what the
// compiler writes; this is what they have in common.

#pragma once

#include <one/screens/source.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <istream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace one::local {

	using value = ember::value;

	// One entity as a step leaves it: all its fields, or that it's gone.
	struct change {
		std::string entity, id;
		value fields = value::map();
		bool removed = false;
	};

	// What a command did, as the log keeps it, one to a line.
	struct event {
		std::string command;
		std::string at;  // when, as RFC 3339 in UTC
		std::vector<change> changes;
	};
	std::string to_line(const event& e);
	std::expected<event, std::string> from_line(std::string_view line);

	// An id nobody chose, as Firestore makes them: twenty letters and digits.
	std::string made_up_id();

	// The entities as the events so far have left them, each holding its id.
	class store {
	public:
		const value* find(const std::string& entity, const std::string& id) const;
		// Each of an entity's rows, in the order of their ids.
		std::vector<const value*> each(const std::string& entity) const;
		void apply(const event& e);
		// Every file a row names, files/<sha-256>, in any of its fields.
		std::set<std::string> files_named() const;

	private:
		std::map<std::string, std::map<std::string, value>> rows_;
	};

	// What a command changes, in one step: it reads the store as it was, with its
	// own changes on top, and nothing is kept until the step is done. What it reads
	// are copies, so a command can change rows while it goes through them.
	class step {
	public:
		explicit step(const store& before) : before_(before) {}

		std::optional<value> find(const std::string& entity, const std::string& id) const;
		std::vector<value> each(const std::string& entity) const;
		// Makes an entity, under the id its fields hold, made from its keys, or one
		// made up when they hold none; its id.
		std::string create(const std::string& entity, value fields);
		// Sets the fields given on an entity that's there, leaving the rest; false
		// when it isn't.
		bool update(const std::string& entity, const std::string& id, const value& fields);
		bool remove(const std::string& entity, const std::string& id);
		// The serial an entity made now gets: one past the highest, counted within
		// what within_field points at when it's given.
		std::int64_t next_serial(const std::string& entity, const std::string& field, const std::string& within_field = "", const std::string& within = "") const;

		const std::vector<change>& changes() const { return changes_; }

	private:
		const change* changed(const std::string& entity, const std::string& id) const;
		void record(change c);

		const store& before_;
		std::vector<change> changes_;
	};

	// SHA-256 of everything read from in, as 64 lowercase hex digits.
	std::string sha256(std::istream& in);

	// A project's folder, $XDG_DATA_HOME/<project>, or ~/.local/share/<project>, held
	// by this process alone while it's open, so its log never has two writers:
	//     events.jsonl   every event, one per line
	//     files/         each file kept, named by its SHA-256
	//     lock           what holds it
	class folder {
	public:
		static std::expected<folder, std::string> open(const std::string& project);
		static std::expected<folder, std::string> open_at(const std::filesystem::path& root);
		static std::filesystem::path where(const std::string& project);

		folder(folder&& other) noexcept;
		folder& operator=(folder&& other) noexcept;
		folder(const folder&) = delete;
		folder& operator=(const folder&) = delete;
		~folder();

		const std::filesystem::path& root() const { return root_; }
		// Every event kept, in the order they happened. A last line cut short, by a
		// crash while it was written, is left out: its command never answered.
		std::expected<std::vector<event>, std::string> read() const;
		// Adds an event to the log, on disk before it returns.
		std::expected<void, std::string> append(const event& e);
		// Copies a file in, once however often it's kept; what names it from now on,
		// files/<sha-256>.
		std::expected<std::string, std::string> keep(const std::filesystem::path& file);
		// Where a file named that way is.
		std::filesystem::path file(const std::string& kept) const { return root_ / kept; }
		// Every kept file but those named let go of, as nothing names them any more;
		// how many.
		std::size_t forget_files_but(const std::set<std::string>& named);

	private:
		folder(std::filesystem::path root, int lock) : root_(std::move(root)), lock_(lock) {}

		std::filesystem::path root_;
		int lock_ = -1;
	};

	// A project's views and commands, run in the app on what its folder keeps: a
	// command's changes are logged, then kept, then every view someone's listening to
	// is built again, and each that changed is told. Everything happens on the thread
	// that calls it, the app's loop.
	class local_source : public screens::source {
	public:
		using command = std::function<screens::answer(const value& fields, step& changes)>;
		// A view for its subject, the entity it's per, or none when there's nothing
		// to show.
		using view = std::function<std::optional<value>(const store& entities, const std::string& subject)>;

		// The folder's events read through, or why they couldn't be.
		static std::expected<std::unique_ptr<local_source>, std::string> open(folder kept);

		void on(const std::string& command_name, command handler) { commands_[command_name] = std::move(handler); }
		void define(const std::string& view_name, view built) { views_[view_name] = std::move(built); }

		const store& entities() const { return store_; }
		const local::folder& kept() const { return folder_; }

		screens::listening listen(const std::string& view, const std::string& subject, std::function<void(const screens::view_state&)> told) override;
		// Commands, and two of the source's own: one::undo takes back the last step
		// taken while the app runs, and one::redo takes it again, each answering the
		// command it undid or redid, and each kept as an event like any other. A step
		// is a command, unless the command's fields say "one:history": "joined", which
		// makes it part of the step before, as one action that runs several does; or
		// "apart", which leaves it out of the history, as bookkeeping like a preview
		// is. Undoing an update puts back only the fields it changed, so a change made
		// apart since stays.
		using screens::source::run;
		void run(const std::string& command_name, const value& fields, std::function<void(const screens::answer&)> answered) override;
		// Each file kept in the folder first, its field then holding files/<sha-256>.
		void run(const std::string& command_name, const value& fields, const screens::attached_files& files, std::function<void(const screens::answer&)> answered) override;

	private:
		explicit local_source(folder kept) : folder_(std::move(kept)) {}

		struct listener {
			std::string view, subject;
			std::function<void(const screens::view_state&)> told;
			std::string last;  // what it was told last, as JSON, so it's told only of a change
		};
		screens::view_state build(const std::string& view, const std::string& subject) const;
		void tell(listener& l);

		// How to put one row back as it was: its fields that changed, their values
		// before (null where a field wasn't there); or removed, when it was made; or all
		// of it, when it was removed.
		struct putting_back {
			std::string entity, id;
			enum { fields, removing, making } how = fields;
			value was = value::map();
		};
		struct history_step {
			std::string command;
			std::vector<putting_back> back;
		};
		std::vector<putting_back> back_from(const std::vector<change>& changes) const;
		std::vector<change> changes_for(const std::vector<putting_back>& back) const;
		void tell_all();
		void take_back(std::vector<history_step>& from, std::vector<history_step>& onto, const std::string& name, std::function<void(const screens::answer&)> answered);

		local::folder folder_;
		store store_;
		std::map<std::string, command> commands_;
		std::map<std::string, view> views_;
		std::map<int, listener> listeners_;
		std::vector<history_step> undoing_, redoing_;  // steps to take back, and taken back to take again
		int next_ = 0;
		std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
	};

}  // namespace one::local
