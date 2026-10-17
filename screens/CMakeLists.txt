# Copyright 2026 Daher Alfawares
# SPDX-License-Identifier: AGPL-3.0-only

# one::screens: what a native app's screens are and do, whatever draws them, shared
# by one::desktop::runtime and one::terminal::runtime. Its views are read through
# one::client, from uione's client/ beside it. Its headers state.hpp and
# editing.hpp, with src/editing.cpp, are what the toolkits build their views with,
# and each compiles them itself, needing nothing else of it.

cmake_minimum_required(VERSION 3.28)
project(one_screens VERSION 0.16.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT TARGET one_client)
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../client ${CMAKE_CURRENT_BINARY_DIR}/client)
endif()

add_library(one_screens
	src/client_source.cpp
	src/memory_source.cpp
	src/models.cpp
	src/screens.cpp)
add_library(one::screens ALIAS one_screens)
target_include_directories(one_screens PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(one_screens PUBLIC one_client)
target_compile_options(one_screens PRIVATE -Wall -Wextra -Werror -Wno-free-nonheap-object)

if(PROJECT_IS_TOP_LEVEL)
	add_executable(tests tests/tests.cpp)
	target_link_libraries(tests PRIVATE one_screens)
	target_compile_options(tests PRIVATE -Wall -Wextra -Werror -Wno-free-nonheap-object)
endif()
