# Copyright 2026 Daher Alfawares
# SPDX-License-Identifier: AGPL-3.0-only

# one::desktop: uione's toolkit for desktop apps, in modern C++ on Wayland, drawing
# with Vulkan. It began as libloom. Built with g++ and
# the system's Wayland, Vulkan, HarfBuzz, FreeType, fontconfig and xkbcommon; nothing
# is downloaded. Wayland's protocol glue is written by wayland-scanner as it builds.

cmake_minimum_required(VERSION 3.28)
project(one_desktop VERSION 0.18.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(VULKAN REQUIRED IMPORTED_TARGET vulkan)
pkg_check_modules(TEXT REQUIRED IMPORTED_TARGET harfbuzz freetype2 fontconfig)
pkg_check_modules(KEYS REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(BUS REQUIRED IMPORTED_TARGET dbus-1)
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
foreach(shader box.vert box.frag glyph.vert glyph.frag picture.vert picture.frag)
	set(output ${CMAKE_CURRENT_BINARY_DIR}/shaders/${shader}.spv)
	add_custom_command(OUTPUT ${output}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
		COMMAND ${GLSLC} --target-env=vulkan1.3 -Werror -O ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader} -o ${output}
		DEPENDS shaders/${shader})
	list(APPEND shader_outputs ${output})
endforeach()
add_custom_target(one_desktop_shaders DEPENDS ${shader_outputs})
set_source_files_properties(src/renderer.cpp PROPERTIES OBJECT_DEPENDS "${shader_outputs}")

# stb_image and stb_image_write, pinned: another project's, kept as it was fetched, so
# a changed copy stops the build rather than being compiled.
foreach(pinned "stb_image.h=594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3"
               "stb_image_write.h=cbd5f0ad7a9cf4468affb36354a1d2338034f2c12473cf1a8e32053cb6914a05")
	string(REPLACE "=" ";" pinned "${pinned}")
	list(GET pinned 0 name)
	list(GET pinned 1 expected)
	file(SHA256 ${CMAKE_CURRENT_SOURCE_DIR}/src/stb/${name} found)
	if(NOT found STREQUAL expected)
		message(FATAL_ERROR "src/stb/${name} isn't the copy pinned at nothings/stb 003501f; its sha256 is ${found}")
	endif()
endforeach()
# nanosvg, pinned the same way (decision 0025): what draws a collage's shapes.
foreach(pinned "nanosvg.h=e34fd5d084be106cea972d19ce5d27fd96d17ba89f8d06bdceee058420c8b2b0"
               "nanosvgrast.h=79a9c5f4db19debf9f3a648a1589e96d92854f245a5cb4f3d823f263785234d8")
	string(REPLACE "=" ";" pinned "${pinned}")
	list(GET pinned 0 name)
	list(GET pinned 1 expected)
	file(SHA256 ${CMAKE_CURRENT_SOURCE_DIR}/src/nanosvg/${name} found)
	if(NOT found STREQUAL expected)
		message(FATAL_ERROR "src/nanosvg/${name} isn't the copy pinned at memononen/nanosvg 239e102; its sha256 is ${found}")
	endif()
endforeach()

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
	src/folder.cpp
	src/portal.cpp
	src/screenshot.cpp
	src/files.cpp
	src/notices.cpp
	src/picture.cpp
	src/svg.cpp
	src/renderer.cpp
	src/seat.cpp
	src/icons.cpp
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
target_link_libraries(one_desktop PRIVATE PkgConfig::WAYLAND PkgConfig::VULKAN PkgConfig::TEXT PkgConfig::KEYS PkgConfig::BUS)
add_library(one::desktop ALIAS one_desktop)
target_compile_options(one_desktop PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Werror --embed-dir=${CMAKE_CURRENT_BINARY_DIR}/shaders>)

# The runtime a desktop app uione writes runs on: its screens, read live through
# one::client from uione's client/ beside it, Markdown through one::markdown, and
# equations through one::math.
option(ONE_DESKTOP_RUNTIME "Build one::desktop::runtime, which needs uione's client/" ON)
if(ONE_DESKTOP_RUNTIME)
	add_library(one_desktop_runtime
		runtime/src/between.cpp
		runtime/src/board.cpp
		runtime/src/calendar.cpp
		runtime/src/canvas.cpp
		runtime/src/collage.cpp
		runtime/src/charts.cpp
		runtime/src/pipeline.cpp
		runtime/src/components.cpp
		runtime/src/forms.cpp
		runtime/src/markdown.cpp
		runtime/src/layout.cpp
		runtime/src/math.cpp
		runtime/src/paper.cpp
		runtime/src/paper/kinds.cpp
		runtime/src/paper/words.cpp
		runtime/src/paper/math.cpp
		runtime/src/paper/code.cpp
		runtime/src/paper/picture.cpp
		runtime/src/runtime.cpp
		runtime/src/table.cpp
		runtime/src/timeline.cpp
		runtime/src/workspace.cpp)
	add_library(one::desktop::runtime ALIAS one_desktop_runtime)
	target_include_directories(one_desktop_runtime PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/runtime/include> $<INSTALL_INTERFACE:include>)
	if(NOT TARGET one_markdown)
		add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../markdown ${CMAKE_CURRENT_BINARY_DIR}/markdown)
	endif()
	if(NOT TARGET one_math)
		add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../math ${CMAKE_CURRENT_BINARY_DIR}/math)
	endif()
	if(NOT TARGET one_screens)
		add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../screens ${CMAKE_CURRENT_BINARY_DIR}/screens)
	endif()
	target_link_libraries(one_desktop_runtime PUBLIC one_desktop one_screens PRIVATE one_markdown one_math)
	target_compile_options(one_desktop_runtime PRIVATE -Wall -Wextra -Werror -Wno-free-nonheap-object)

endif()

# Its examples and tests, when it's built on its own rather than inside an app, or when
# a build of everything asks for them, so the library is compiled once for both.
option(ONE_DESKTOP_EXAMPLES "Build its examples and tests" ${PROJECT_IS_TOP_LEVEL})
if(ONE_DESKTOP_EXAMPLES)
add_executable(hello examples/hello.cpp)
target_link_libraries(hello PRIVATE one_desktop)
target_compile_options(hello PRIVATE -Wall -Wextra -Werror)

add_executable(typing examples/typing.cpp)
target_link_libraries(typing PRIVATE one_desktop)
target_compile_options(typing PRIVATE -Wall -Wextra -Werror)

add_executable(drawing examples/drawing.cpp)
target_link_libraries(drawing PRIVATE one_desktop)
target_compile_options(drawing PRIVATE -Wall -Wextra -Werror)

add_executable(one_desktop_tests tests/tests.cpp)
set_target_properties(one_desktop_tests PROPERTIES OUTPUT_NAME tests)
target_link_libraries(one_desktop_tests PRIVATE one_desktop PkgConfig::VULKAN)
target_include_directories(one_desktop_tests PRIVATE src ${CMAKE_CURRENT_BINARY_DIR}/protocols)
target_compile_definitions(one_desktop_tests PRIVATE ONE_DESKTOP_TESTS="${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_compile_options(one_desktop_tests PRIVATE -Wall -Wextra -Werror)

if(ONE_DESKTOP_RUNTIME)
	add_executable(one_desktop_runtime_tracker runtime/examples/tracker.cpp)
	set_target_properties(one_desktop_runtime_tracker PROPERTIES OUTPUT_NAME runtime_tracker)
	target_link_libraries(one_desktop_runtime_tracker PRIVATE one_desktop_runtime)
	target_compile_options(one_desktop_runtime_tracker PRIVATE -Wall -Wextra -Werror -Wno-free-nonheap-object)

	add_executable(one_desktop_runtime_tests runtime/tests/tests.cpp)
	set_target_properties(one_desktop_runtime_tests PROPERTIES OUTPUT_NAME runtime_tests)
	target_link_libraries(one_desktop_runtime_tests PRIVATE one_desktop_runtime PkgConfig::VULKAN)
	target_include_directories(one_desktop_runtime_tests PRIVATE src tests ${CMAKE_CURRENT_BINARY_DIR}/protocols)
	target_compile_definitions(one_desktop_runtime_tests PRIVATE ONE_DESKTOP_TESTS="${CMAKE_CURRENT_SOURCE_DIR}/runtime/tests")
	target_compile_options(one_desktop_runtime_tests PRIVATE -Wall -Wextra -Werror -Wno-free-nonheap-object)
endif()

add_executable(demo examples/demo.cpp)
target_link_libraries(demo PRIVATE one_desktop)
target_compile_options(demo PRIVATE -Wall -Wextra -Werror)
endif()
