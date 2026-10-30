# Copyright 2026 Daher Alfawares
# SPDX-License-Identifier: AGPL-3.0-only

# one::syntax: code colored as it's read, the same on every target. One header, and its
# test, held to tests/languages.tsv and tests/cases.tsv as @uione/react's syntax.ts is.

cmake_minimum_required(VERSION 3.28)
project(one_syntax VERSION 0.18.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The library: one header, nothing to compile.
add_library(one_syntax INTERFACE)
add_library(one::syntax ALIAS one_syntax)
target_include_directories(one_syntax INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_compile_features(one_syntax INTERFACE cxx_std_23)

# Its test, when it's built on its own, or when a build of everything asks for it.
option(ONE_SYNTAX_EXAMPLES "Build its test" ${PROJECT_IS_TOP_LEVEL})
if(ONE_SYNTAX_EXAMPLES)
	add_executable(one_syntax_tests tests/tests.cpp)
	set_target_properties(one_syntax_tests PROPERTIES OUTPUT_NAME tests)
	target_link_libraries(one_syntax_tests PRIVATE one_syntax)
	target_compile_definitions(one_syntax_tests PRIVATE ONE_SYNTAX_TESTS="${CMAKE_CURRENT_SOURCE_DIR}/tests")
	target_compile_options(one_syntax_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
