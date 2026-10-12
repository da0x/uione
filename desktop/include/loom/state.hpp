// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// State: a value whose readers are known. A window's view reads states while it's
// built; setting one of them builds that view again and draws the window, with
// nothing else to call:
//
//     loom::state<int> count = 0;
//     window.show([=] { return loom::text(std::format("{} issues", *count)); });
//     count.set(3);  // the window says "3 issues" at its next frame
//
// A state is a handle: copies share one value, so a lambda can hold one by value.
// It's read and set on the loop's thread; another thread hands its change to
// app::post.

#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace loom {

	namespace detail {

		// Whoever is reading: a view being built, told when what it read changes.
		struct reader {
			std::function<void()> changed;
		};

		inline std::shared_ptr<reader>& current_reader() {
			thread_local std::shared_ptr<reader> current;
			return current;
		}

		// While a view is built: its reads are noted against `r`.
		class reading {
		public:
			explicit reading(std::shared_ptr<reader> r) : before_(std::exchange(current_reader(), std::move(r))) {}
			~reading() { current_reader() = std::move(before_); }
			reading(const reading&) = delete;
			reading& operator=(const reading&) = delete;

		private:
			std::shared_ptr<reader> before_;
		};

	}  // namespace detail

	template <class T>
	class state {
	public:
		state() : cell_(std::make_shared<cell>()) {}
		state(T value) : cell_(std::make_shared<cell>(std::move(value))) {}

		const T& get() const {
			noted();
			return cell_->value;
		}
		const T& operator*() const { return get(); }
		const T* operator->() const { return &get(); }

		void set(T value) {
			cell_->value = std::move(value);
			tell();
		}
		// Changes the value in place, then tells its readers once.
		template <class F>
		void change(F&& how) {
			std::forward<F>(how)(cell_->value);
			tell();
		}

	private:
		struct cell {
			T value{};
			std::vector<std::weak_ptr<detail::reader>> readers;
		};

		void noted() const {
			const auto& r = detail::current_reader();
			if (!r) return;
			for (const auto& known : cell_->readers) {
				if (known.lock() == r) return;
			}
			cell_->readers.push_back(r);
		}

		void tell() {
			// Each reader once; one that's gone, like a view since built again, is dropped.
			auto readers = std::move(cell_->readers);
			cell_->readers.clear();
			for (auto& weak : readers) {
				if (auto r = weak.lock(); r && r->changed) r->changed();
			}
		}

		std::shared_ptr<cell> cell_;
	};

}  // namespace loom
