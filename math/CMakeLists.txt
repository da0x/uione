# Copyright 2026 Daher Alfawares
# SPDX-License-Identifier: AGPL-3.0-only

# one::math: equations written as they'd be typed, read as a tree to set and work out.
# One header, and its test, held to tests/cases.tsv as @uione/react's math.ts is.

cmake_minimum_required(VERSION 3.28)
project(one_math VERSION 0.18.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The library: one header, nothing to compile.
add_library(one_math INTERFACE)
add_library(one::math ALIAS one_math)
target_include_directories(one_math INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_compile_features(one_math INTERFACE cxx_std_23)

# Its test, when it's built on its own, or when a build of everything asks for it.
option(ONE_MATH_EXAMPLES "Build its test" ${PROJECT_IS_TOP_LEVEL})
if(ONE_MATH_EXAMPLES)
	add_executable(one_math_tests tests/tests.cpp)
	set_target_properties(one_math_tests PROPERTIES OUTPUT_NAME tests)
	target_link_libraries(one_math_tests PRIVATE one_math)
	target_compile_definitions(one_math_tests PRIVATE ONE_MATH_TESTS="${CMAKE_CURRENT_SOURCE_DIR}/tests")
	target_compile_options(one_math_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
