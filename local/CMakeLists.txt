# Copyright 2026 Daher Alfawares
# SPDX-License-Identifier: AGPL-3.0-only

# one::local: what a project that says storage local keeps on its machine, and the
# one::screens::source its screens read it through (decision 0015). It needs
# one::screens, from uione's screens/ beside it, for that source.

cmake_minimum_required(VERSION 3.28)
project(one_local VERSION 0.18.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT TARGET one_screens)
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../screens ${CMAKE_CURRENT_BINARY_DIR}/screens)
endif()

add_library(one_local
	src/events.cpp
	src/folder.cpp
	src/local_source.cpp
	src/store.cpp)
add_library(one::local ALIAS one_local)
target_include_directories(one_local PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(one_local PUBLIC one_screens)
target_compile_options(one_local PRIVATE -Wall -Wextra -Werror -Wno-free-nonheap-object)

# Its tests, when it's built on its own or a build of everything asks for them.
option(ONE_LOCAL_EXAMPLES "Build its tests" ${PROJECT_IS_TOP_LEVEL})
if(ONE_LOCAL_EXAMPLES)
	add_executable(one_local_tests tests/tests.cpp)
	set_target_properties(one_local_tests PROPERTIES OUTPUT_NAME tests)
	target_link_libraries(one_local_tests PRIVATE one_local)
	target_compile_options(one_local_tests PRIVATE -Wall -Wextra -Werror -Wno-free-nonheap-object)
endif()
