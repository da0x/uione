# Copyright 2026 Daher Alfawares
# SPDX-License-Identifier: Apache-2.0

# libloom: a modern C++ toolkit for Wayland, drawing with Vulkan. Built with g++ and
# the system's Wayland, Vulkan, HarfBuzz, FreeType, fontconfig and xkbcommon; nothing
# is downloaded. Wayland's protocol glue is written by wayland-scanner as it builds.

cmake_minimum_required(VERSION 3.28)
project(loom VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(VULKAN REQUIRED IMPORTED_TARGET vulkan)
pkg_get_variable(PROTOCOLS wayland-protocols pkgdatadir)
pkg_get_variable(SCANNER wayland-scanner wayland_scanner)

# Each protocol the windows speak: its client header and the interface tables.
set(protocols
	stable/xdg-shell/xdg-shell.xml
	stable/viewporter/viewporter.xml
	staging/fractional-scale/fractional-scale-v1.xml
	unstable/xdg-decoration/xdg-decoration-unstable-v1.xml)
set(protocol_sources)
foreach(protocol ${protocols})
	get_filename_component(name ${protocol} NAME_WE)
	set(header ${CMAKE_CURRENT_BINARY_DIR}/protocols/${name}-client-protocol.h)
	set(code ${CMAKE_CURRENT_BINARY_DIR}/protocols/${name}-protocol.c)
	add_custom_command(OUTPUT ${header} ${code}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/protocols
		COMMAND ${SCANNER} client-header ${PROTOCOLS}/${protocol} ${header}
		COMMAND ${SCANNER} private-code ${PROTOCOLS}/${protocol} ${code}
		DEPENDS ${PROTOCOLS}/${protocol})
	list(APPEND protocol_sources ${header} ${code})
endforeach()

add_library(loom
	src/app.cpp
	src/gpu.cpp
	${protocol_sources})
target_include_directories(loom
	PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
	PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/protocols)
target_link_libraries(loom PRIVATE PkgConfig::WAYLAND PkgConfig::VULKAN)
target_compile_options(loom PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Werror>)

add_executable(hello examples/hello.cpp)
target_link_libraries(hello PRIVATE loom)
target_compile_options(hello PRIVATE -Wall -Wextra -Werror)
