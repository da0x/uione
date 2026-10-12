// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A small test harness: TEST("what it shows") { CHECK(condition); }, each test run
// in turn, every failed check said with its file and line, and main's exit code the
// count of tests that failed.

#pragma once

#include <format>
#include <functional>
#include <print>
#include <source_location>
#include <string>
#include <vector>

namespace check {

	struct test {
		std::string name;
		std::function<void()> body;
	};

	inline std::vector<test>& tests() {
		static std::vector<test> all;
		return all;
	}
	inline int& failures() {
		static int count = 0;
		return count;
	}

	struct adding {
		adding(std::string name, std::function<void()> body) { tests().push_back({std::move(name), std::move(body)}); }
	};

	inline void verify(bool held, const char* what, const std::string& note = {}, std::source_location at = std::source_location::current()) {
		if (held) return;
		++failures();
		std::println(stderr, "  {}:{}: {}{}", at.file_name(), at.line(), what, note.empty() ? "" : "  (" + note + ")");
	}

	inline int run() {
		int failed = 0;
		for (const auto& t : tests()) {
			int before = failures();
			t.body();
			bool passed = failures() == before;
			if (!passed) ++failed;
			std::println("{} {}", passed ? "ok  " : "FAIL", t.name);
		}
		std::println("{} of {} passed", tests().size() - failed, tests().size());
		return failed;
	}

}  // namespace check

#define CHECK_JOIN2(a, b) a##b
#define CHECK_JOIN(a, b) CHECK_JOIN2(a, b)
#define TEST(name) \
	static void CHECK_JOIN(test_, __LINE__)(); \
	static check::adding CHECK_JOIN(adding_, __LINE__){name, CHECK_JOIN(test_, __LINE__)}; \
	static void CHECK_JOIN(test_, __LINE__)()
#define CHECK(condition) check::verify(static_cast<bool>(condition), #condition)
#define CHECK_SAYING(condition, note) check::verify(static_cast<bool>(condition), #condition, note)
