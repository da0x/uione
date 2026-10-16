# Copyright 2026 Daher Alfawares
# SPDX-License-Identifier: AGPL-3.0-only

# one::desktop: uione's toolkit for desktop apps, in modern C++ on Wayland, drawing
# with Vulkan. It began as libloom. Built with g++ and
# the system's Wayland, Vulkan, HarfBuzz, FreeType, fontconfig and xkbcommon; nothing
# is downloaded. Wayland's protocol glue is written by wayland-scanner as it builds.

cmake_minimum_required(VERSION 3.28)
project(one_desktop VERSION 0.15.1 LANGUAGES C CXX)

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
	staging/cursor-shape/cursor-shape-v1.xml
	unstable/text-input/text-input-unstable-v3.xml)
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
add_custom_target(one_desktop_shaders DEPENDS ${shader_outputs})
set_source_files_properties(src/renderer.cpp PROPERTIES OBJECT_DEPENDS "${shader_outputs}")

add_library(one_desktop
	src/app.cpp
	src/choices.cpp
	src/clipboard.cpp
	src/dragging.cpp
	src/surface.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../screens/src/editing.cpp
	src/field.cpp
	src/gpu.cpp
	src/offscreen.cpp
	src/renderer.cpp
	src/seat.cpp
	src/text.cpp
	src/text_input.cpp
	src/ui.cpp
	src/view.cpp
	${protocol_sources})
add_dependencies(one_desktop one_desktop_shaders)
# Views are built from one::screens' states, a header of uione's screens/ beside it.
target_include_directories(one_desktop
	PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../screens/include> $<INSTALL_INTERFACE:include>
	PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/protocols)
target_link_libraries(one_desktop PRIVATE PkgConfig::WAYLAND PkgConfig::VULKAN PkgConfig::TEXT PkgConfig::KEYS)
add_library(one::desktop ALIAS one_desktop)
target_compile_options(one_desktop PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Werror --embed-dir=${CMAKE_CURRENT_BINARY_DIR}/shaders>)

# The runtime a desktop app uione writes runs on: its screens, read live through
# one::client from uione's client/ beside it, and Markdown through one::markdown.
option(ONE_DESKTOP_RUNTIME "Build one::desktop::runtime, which needs uione's client/" ON)
if(ONE_DESKTOP_RUNTIME)
	add_library(one_desktop_runtime
		runtime/src/between.cpp
		runtime/src/board.cpp
		runtime/src/canvas.cpp
		runtime/src/pipeline.cpp
		runtime/src/components.cpp
		runtime/src/forms.cpp
		runtime/src/markdown.cpp
		runtime/src/layout.cpp
		runtime/src/runtime.cpp
		runtime/src/table.cpp
		runtime/src/timeline.cpp)
	add_library(one::desktop::runtime ALIAS one_desktop_runtime)
	target_include_directories(one_desktop_runtime PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/runtime/include> $<INSTALL_INTERFACE:include>)
	if(NOT TARGET one_markdown)
		add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../markdown ${CMAKE_CURRENT_BINARY_DIR}/markdown)
	endif()
	if(NOT TARGET one_screens)
		add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../screens ${CMAKE_CURRENT_BINARY_DIR}/screens)
	endif()
	target_link_libraries(one_desktop_runtime PUBLIC one_desktop one_screens PRIVATE one_markdown)
	target_compile_options(one_desktop_runtime PRIVATE -Wall -Wextra -Werror -Wno-free-nonheap-object)

endif()

# Its examples and tests, when it's built on its own rather than inside an app.
if(PROJECT_IS_TOP_LEVEL)
add_executable(hello examples/hello.cpp)
target_link_libraries(hello PRIVATE one_desktop)
target_compile_options(hello PRIVATE -Wall -Wextra -Werror)

add_executable(typing examples/typing.cpp)
target_link_libraries(typing PRIVATE one_desktop)
target_compile_options(typing PRIVATE -Wall -Wextra -Werror)

add_executable(drawing examples/drawing.cpp)
target_link_libraries(drawing PRIVATE one_desktop)
target_compile_options(drawing PRIVATE -Wall -Wextra -Werror)

add_executable(tests tests/tests.cpp)
target_link_libraries(tests PRIVATE one_desktop PkgConfig::VULKAN)
target_include_directories(tests PRIVATE src ${CMAKE_CURRENT_BINARY_DIR}/protocols)
target_compile_definitions(tests PRIVATE ONE_DESKTOP_TESTS="${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_compile_options(tests PRIVATE -Wall -Wextra -Werror)

if(ONE_DESKTOP_RUNTIME)
	add_executable(runtime_tracker runtime/examples/tracker.cpp)
	target_link_libraries(runtime_tracker PRIVATE one_desktop_runtime)
	target_compile_options(runtime_tracker PRIVATE -Wall -Wextra -Werror -Wno-free-nonheap-object)

	add_executable(runtime_tests runtime/tests/tests.cpp)
	target_link_libraries(runtime_tests PRIVATE one_desktop_runtime PkgConfig::VULKAN)
	target_include_directories(runtime_tests PRIVATE src tests ${CMAKE_CURRENT_BINARY_DIR}/protocols)
	target_compile_definitions(runtime_tests PRIVATE ONE_DESKTOP_TESTS="${CMAKE_CURRENT_SOURCE_DIR}/runtime/tests")
	target_compile_options(runtime_tests PRIVATE -Wall -Wextra -Werror -Wno-free-nonheap-object)
endif()

add_executable(demo examples/demo.cpp)
target_link_libraries(demo PRIVATE one_desktop)
target_compile_options(demo PRIVATE -Wall -Wextra -Werror)
endif()
