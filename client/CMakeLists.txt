# Copyright 2026 Daher Alfawares
# SPDX-License-Identifier: AGPL-3.0-only

# one::client: how a desktop or terminal app uione writes reaches its app, on libember
# (its header pinned here, in include/ember.hpp). TLS is Botan 3 unless
# ONE_CLIENT_TLS is openssl.

cmake_minimum_required(VERSION 3.28)
project(one_client VERSION 0.17.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(ONE_CLIENT_TLS botan CACHE STRING "botan or openssl")

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
add_library(one_client src/client.cpp src/agent.cpp)
add_library(one::client ALIAS one_client)
target_include_directories(one_client PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
if(ONE_CLIENT_TLS STREQUAL "openssl")
	pkg_check_modules(TLS REQUIRED IMPORTED_TARGET openssl)
	target_compile_definitions(one_client PUBLIC EMBER_TLS_OPENSSL)
else()
	pkg_check_modules(TLS REQUIRED IMPORTED_TARGET botan-3)
endif()
target_link_libraries(one_client PUBLIC PkgConfig::TLS Threads::Threads)
# GCC 16 wrongly says ember's HTTP code frees memory it didn't allocate, once inlined.
target_compile_options(one_client PRIVATE -Wall -Wextra -Werror -Wno-free-nonheap-object)

# Its tests, when it's built on its own or a build of everything asks for them:
# tests/run starts the tasks example's backend on the emulators and runs them against it.
option(ONE_CLIENT_EXAMPLES "Build its tests" ${PROJECT_IS_TOP_LEVEL})
if(ONE_CLIENT_EXAMPLES)
	add_executable(one_client_tests tests/tests.cpp)
	set_target_properties(one_client_tests PROPERTIES OUTPUT_NAME tests)
	target_link_libraries(one_client_tests PRIVATE one_client)
	target_compile_options(one_client_tests PRIVATE -Wall -Wextra -Werror -Wno-free-nonheap-object)
endif()
