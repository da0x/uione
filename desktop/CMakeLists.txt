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
pkg_check_modules(TEXT REQUIRED IMPORTED_TARGET harfbuzz freetype2 fontconfig)
pkg_check_modules(KEYS REQUIRED IMPORTED_TARGET xkbcommon)
find_program(GLSLC glslc REQUIRED)
pkg_get_variable(PROTOCOLS wayland-protocols pkgdatadir)
pkg_get_variable(SCANNER wayland-scanner wayland_scanner)

# Each protocol the windows speak: its client header and the interface tables.
set(protocols
	stable/xdg-shell/xdg-shell.xml
	stable/viewporter/viewporter.xml
	staging/fractional-scale/fractional-scale-v1.xml
	unstable/xdg-decoration/xdg-decoration-unstable-v1.xml
	unstable/tablet/tablet-unstable-v2.xml
	staging/cursor-shape/cursor-shape-v1.xml)
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

# The shaders, compiled to SPIR-V, which the renderer takes in with #embed.
set(shader_outputs)
foreach(shader box.vert box.frag glyph.vert glyph.frag)
	set(output ${CMAKE_CURRENT_BINARY_DIR}/shaders/${shader}.spv)
	add_custom_command(OUTPUT ${output}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
		COMMAND ${GLSLC} --target-env=vulkan1.3 -Werror -O ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader} -o ${output}
		DEPENDS shaders/${shader})
	list(APPEND shader_outputs ${output})
endforeach()
add_custom_target(loom_shaders DEPENDS ${shader_outputs})
set_source_files_properties(src/renderer.cpp PROPERTIES OBJECT_DEPENDS "${shader_outputs}")

add_library(loom
	src/app.cpp
	src/gpu.cpp
	src/offscreen.cpp
	src/renderer.cpp
	src/seat.cpp
	src/text.cpp
	src/ui.cpp
	src/view.cpp
	${protocol_sources})
add_dependencies(loom loom_shaders)
target_include_directories(loom
	PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
	PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/protocols)
target_link_libraries(loom PRIVATE PkgConfig::WAYLAND PkgConfig::VULKAN PkgConfig::TEXT PkgConfig::KEYS)
target_compile_options(loom PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Werror --embed-dir=${CMAKE_CURRENT_BINARY_DIR}/shaders>)

# Its examples and tests, when it's built on its own rather than inside an app.
if(PROJECT_IS_TOP_LEVEL)
add_executable(hello examples/hello.cpp)
target_link_libraries(hello PRIVATE loom)
target_compile_options(hello PRIVATE -Wall -Wextra -Werror)

add_executable(drawing examples/drawing.cpp)
target_link_libraries(drawing PRIVATE loom)
target_compile_options(drawing PRIVATE -Wall -Wextra -Werror)

add_executable(tests tests/tests.cpp)
target_link_libraries(tests PRIVATE loom PkgConfig::VULKAN)
target_include_directories(tests PRIVATE src ${CMAKE_CURRENT_BINARY_DIR}/protocols)
target_compile_definitions(tests PRIVATE LOOM_TESTS="${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_compile_options(tests PRIVATE -Wall -Wextra -Werror)

add_executable(demo examples/demo.cpp)
target_link_libraries(demo PRIVATE loom)
target_compile_options(demo PRIVATE -Wall -Wextra -Werror)
endif()
