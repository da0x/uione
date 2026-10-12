// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// An app's own backend as a desktop app's source: views read live through
// one::client, whose session delivers on the loop, and commands sent one after
// another on a thread of their own, so the window keeps drawing while one is on its
// way; each answer comes back on the loop.

#pragma once

#include <one/client/client.hpp>

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "one/screens/source.hpp"

namespace one::screens {

	class client_source : public source {
	public:
		// `deliver` hands work to the loop, as one::desktop::app::post does.
		client_source(one::client::session& session, std::function<void(std::function<void()>)> deliver);
		~client_source() override;

		listening listen(const std::string& view, const std::string& subject, std::function<void(const view_state&)> told) override;
		void run(const std::string& command, const value& fields, std::function<void(const answer&)> answered) override;
		std::optional<person> reader() const override { return reader_; }

	private:
		one::client::session& session_;
		std::function<void(std::function<void()>)> deliver_;
		std::optional<person> reader_;
		std::mutex lock_;
		std::condition_variable waiting_;
		std::deque<std::function<void()>> sending_;
		bool stopping_ = false;
		std::thread sender_;
	};

}  // namespace one::screens
