# Copyright 2026 Daher Alfawares
# SPDX-License-Identifier: AGPL-3.0-only

# one::terminal: uione's toolkit for terminal apps, drawn with ncurses. Built with g++
# and the system's ncursesw; nothing is downloaded.

cmake_minimum_required(VERSION 3.28)
project(one_terminal VERSION 0.9.8 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURSES REQUIRED IMPORTED_TARGET ncursesw)

add_library(one_terminal src/terminal.cpp)
add_library(one::terminal ALIAS one_terminal)
target_include_directories(one_terminal PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(one_terminal PRIVATE PkgConfig::CURSES)
target_compile_options(one_terminal PRIVATE -Wall -Wextra -Werror)

# Its example, when it's built on its own rather than inside an app.
if(PROJECT_IS_TOP_LEVEL)
	find_package(Threads REQUIRED)
	add_executable(issues examples/issues.cpp)
	target_link_libraries(issues PRIVATE one_terminal Threads::Threads)
	target_compile_options(issues PRIVATE -Wall -Wextra -Werror)
endif()
