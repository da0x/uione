# Copyright 2026 Daher Alfawares
# SPDX-License-Identifier: AGPL-3.0-only

# one::markdown: Markdown laid out for terminals, and read as a tree for windows. One
# header, and md, the command that prints Markdown in a terminal. It began as
# libmarkdown.

cmake_minimum_required(VERSION 3.28)
project(one_markdown VERSION 0.15.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The library: one header, nothing to compile.
add_library(one_markdown INTERFACE)
add_library(one::markdown ALIAS one_markdown)
target_include_directories(one_markdown INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_compile_features(one_markdown INTERFACE cxx_std_23)

# md and its test, when it's built on its own rather than inside an app.
if(PROJECT_IS_TOP_LEVEL)
	add_executable(md src/main.cpp)
	target_link_libraries(md PRIVATE one_markdown)
	target_compile_options(md PRIVATE -Wall -Wextra -Wpedantic -Werror)
	add_executable(layout_test test/layout_test.cpp)
	target_link_libraries(layout_test PRIVATE one_markdown)
	target_compile_options(layout_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
	install(TARGETS md RUNTIME DESTINATION bin)
endif()
