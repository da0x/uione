// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "one/screens/client_source.hpp"

namespace one::screens {

	client_source::client_source(one::client::session& session, std::function<void(std::function<void()>)> deliver)
	    : session_(session), deliver_(std::move(deliver)) {
		if (session_.signed_in()) {
			try {
				reader_ = person{session_.uid(), session_.me().name};
			} catch (const std::exception&) {
				reader_ = person{session_.uid(), ""};
			}
		}
		sender_ = std::thread([this] {
			for (;;) {
				std::function<void()> next;
				{
					std::unique_lock hold(lock_);
					waiting_.wait(hold, [this] { return stopping_ || !sending_.empty(); });
					if (sending_.empty()) return;
					next = std::move(sending_.front());
					sending_.pop_front();
				}
				next();
			}
		});
	}

	client_source::~client_source() {
		{
			std::lock_guard hold(lock_);
			stopping_ = true;
			sending_.clear();
		}
		waiting_.notify_one();
		sender_.join();
	}

	listening client_source::listen(const std::string& view, const std::string& subject, std::function<void(const view_state&)> told) {
		auto registration = std::make_shared<ember::database::registration>(session_.listen(
		    view, subject,
		    [told](const ember::snapshot& s) {
			    told(s.exists ? view_state{status::ready, s.data, ""} : view_state{status::missing, value::map(), ""});
		    },
		    [told](const ember::error& e) { told(view_state{status::failed, value::map(), e.message}); }));
		return listening([registration]() mutable { registration.reset(); });
	}

	void client_source::run(const std::string& command, const value& fields, std::function<void(const answer&)> answered) {
		{
			std::lock_guard hold(lock_);
			sending_.push_back([this, command, fields, answered = std::move(answered)] {
				answer got;
				try {
					got = session_.run(command, fields);
				} catch (const std::exception& e) {
					got = std::unexpected(std::string(e.what()));
				}
				deliver_([answered, got] { answered(got); });
			});
		}
		waiting_.notify_one();
	}

}  // namespace one::screens
