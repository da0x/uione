# Copyright 2026 Daher Alfawares
# SPDX-License-Identifier: AGPL-3.0-only

# one::terminal: uione's toolkit for terminal apps, drawn with ncurses. Built with g++
# and the system's ncursesw; nothing is downloaded.

cmake_minimum_required(VERSION 3.28)
project(one_terminal VERSION 0.10.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURSES REQUIRED IMPORTED_TARGET ncursesw)

add_library(one_terminal src/terminal.cpp src/ui.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../screens/src/editing.cpp)
add_library(one::terminal ALIAS one_terminal)
# Views are built from one::screens' states and fields edit as its editing does,
# from uione's screens/ beside it.
target_include_directories(one_terminal PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../screens/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(one_terminal PRIVATE PkgConfig::CURSES)
target_compile_options(one_terminal PRIVATE -Wall -Wextra -Werror)

# Its example, when it's built on its own rather than inside an app.
# The runtime a terminal app uione writes runs on: one::screens' screens, drawn in
# cells, with Markdown through one::markdown, from uione's screens/ and markdown/.
option(ONE_TERMINAL_RUNTIME "Build one::terminal::runtime, which needs uione's screens/ and client/" ON)
if(ONE_TERMINAL_RUNTIME)
	if(NOT TARGET one_screens)
		add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../screens ${CMAKE_CURRENT_BINARY_DIR}/screens)
	endif()
	if(NOT TARGET one_markdown)
		add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../markdown ${CMAKE_CURRENT_BINARY_DIR}/markdown)
	endif()
	add_library(one_terminal_runtime runtime/src/components.cpp runtime/src/runtime.cpp)
	add_library(one::terminal::runtime ALIAS one_terminal_runtime)
	target_include_directories(one_terminal_runtime PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/runtime/include> $<INSTALL_INTERFACE:include>)
	target_link_libraries(one_terminal_runtime PUBLIC one_terminal one_screens PRIVATE one_markdown)
	target_compile_options(one_terminal_runtime PRIVATE -Wall -Wextra -Werror -Wno-free-nonheap-object)
endif()

if(PROJECT_IS_TOP_LEVEL)
	find_package(Threads REQUIRED)
	if(ONE_TERMINAL_RUNTIME)
		add_executable(runtime_tracker runtime/examples/tracker.cpp)
		target_link_libraries(runtime_tracker PRIVATE one_terminal_runtime)
		target_compile_options(runtime_tracker PRIVATE -Wall -Wextra -Werror -Wno-free-nonheap-object)
		add_executable(runtime_tests runtime/tests/tests.cpp)
		target_link_libraries(runtime_tests PRIVATE one_terminal_runtime)
		target_compile_definitions(runtime_tests PRIVATE ONE_TERMINAL_TESTS="${CMAKE_CURRENT_SOURCE_DIR}/runtime/tests")
		target_compile_options(runtime_tests PRIVATE -Wall -Wextra -Werror -Wno-free-nonheap-object)
	endif()
	add_executable(issues examples/issues.cpp)
	target_link_libraries(issues PRIVATE one_terminal Threads::Threads)
	target_compile_options(issues PRIVATE -Wall -Wextra -Werror)
endif()
