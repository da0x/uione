// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// Owning what C libraries hand out: a pointer freed by the library's own function, and
// a file descriptor closed, each when its owner goes. Members owning these are freed
// in the reverse of the order they're declared in, so a struct's declarations say
// the order things are torn down in, and nothing is freed by hand.

#pragma once

#include <unistd.h>

#include <utility>

namespace one::desktop {

	// A pointer freed by `destroy`, like owned<wl_surface, wl_surface_destroy>. It reads
	// as the pointer it holds, so it's handed to the library's functions as it is.
	template <class T, auto destroy>
	class owned {
	public:
		owned() = default;
		owned(T* held) : held_(held) {}
		owned(owned&& other) noexcept : held_(std::exchange(other.held_, nullptr)) {}
		owned& operator=(owned&& other) noexcept {
			reset(std::exchange(other.held_, nullptr));
			return *this;
		}
		owned& operator=(T* held) {
			reset(held);
			return *this;
		}
		owned(const owned&) = delete;
		owned& operator=(const owned&) = delete;
		~owned() { reset(); }

		void reset(T* held = nullptr) {
			if (held_ && held_ != held) destroy(held_);
			held_ = held;
		}
		T* get() const { return held_; }
		operator T*() const { return held_; }

	private:
		T* held_ = nullptr;
	};

	// A file descriptor, closed when it goes.
	class owned_descriptor {
	public:
		owned_descriptor() = default;
		explicit owned_descriptor(int held) : held_(held) {}
		owned_descriptor(owned_descriptor&& other) noexcept : held_(std::exchange(other.held_, -1)) {}
		owned_descriptor& operator=(owned_descriptor&& other) noexcept {
			if (this != &other) {
				reset();
				held_ = std::exchange(other.held_, -1);
			}
			return *this;
		}
		owned_descriptor(const owned_descriptor&) = delete;
		owned_descriptor& operator=(const owned_descriptor&) = delete;
		~owned_descriptor() { reset(); }

		void reset() {
			if (held_ >= 0) ::close(held_);
			held_ = -1;
		}
		int get() const { return held_; }

	private:
		int held_ = -1;
	};

}  // namespace one::desktop
