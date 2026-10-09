# Copyright 2026 Daher Alfawares
# SPDX-License-Identifier: AGPL-3.0-only

cmake_minimum_required(VERSION 3.25)
project(one LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# uione's own library, library/one.one, built into the compiler as text: what a
# project block may say, and the values it takes. Changing it configures again.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/library/one.one ONE_LIBRARY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/library/library.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/language/library.hpp @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/library/one.one)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/generated)

# Warnings are errors, so a warning can't be ignored until it becomes a bug.
add_library(one_warnings INTERFACE)
target_compile_options(one_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)

include(FetchContent)

# md4c turns the docs' markdown into HTML at build time. Like doctest, it's downloaded
# at a pinned version rather than kept in the repository. It's someone else's C, so
# it's built without this project's warnings-as-errors.
FetchContent_Declare(md4c
    URL https://github.com/mity/md4c/archive/refs/tags/v0.6.0.tar.gz
    URL_HASH SHA256=4d151298125a81da3b2efa2e0eed8bdb7a9318569804e4fa4d7a2375ab83ef70
    SOURCE_SUBDIR not-built-by-its-own-cmake)
FetchContent_MakeAvailable(md4c)
add_library(md4c STATIC
    ${md4c_SOURCE_DIR}/src/md4c.c
    ${md4c_SOURCE_DIR}/src/md4c-html.c
    ${md4c_SOURCE_DIR}/src/entity.c)
target_include_directories(md4c SYSTEM PUBLIC ${md4c_SOURCE_DIR}/src)

add_executable(one main.cpp)
target_include_directories(one PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(one PRIVATE one_warnings md4c)

# For the browser: the same compiler, called from JavaScript through wasm.cpp, with
# a project's files in an in-memory file system. Built with Emscripten, through
# tools/wasm/build.
if(EMSCRIPTEN)
    add_executable(one_wasm wasm.cpp)
    target_include_directories(one_wasm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(one_wasm PRIVATE one_warnings md4c)
    set_target_properties(one_wasm PROPERTIES OUTPUT_NAME one SUFFIX .mjs)
    # The parser stops at a mistake by throwing, so exceptions have to work, and
    # WebAssembly's own are the fastest and smallest way.
    target_compile_options(one_wasm PRIVATE -fwasm-exceptions)
    target_link_options(one_wasm PRIVATE
        -fwasm-exceptions
        -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=load
        -sENVIRONMENT=web,worker,node
        -sALLOW_MEMORY_GROWTH=1
        -sFORCE_FILESYSTEM=1 -lnodefs.js
        -sEXPORTED_RUNTIME_METHODS=FS,ccall,cwrap,UTF8ToString
        -sEXPORTED_FUNCTIONS=_one_version,_one_check,_one_build,_one_show,_one_define,_one_complete,_one_themes)
endif()

option(ONE_TESTS "Build the compiler's tests" ON)
if(ONE_TESTS)
    # doctest is downloaded at a pinned version rather than kept in the repository,
    # which holds only its own work. Only its single header is used.
    FetchContent_Declare(doctest
        URL https://github.com/doctest/doctest/archive/refs/tags/v2.5.3.tar.gz
        URL_HASH SHA256=174ebc4e769928959614789c5b4e9c3d0a0f81a62bb608756b127bfebfb21331
        SOURCE_SUBDIR header-only)
    FetchContent_MakeAvailable(doctest)

    enable_testing()
    add_executable(one_tests
        tests/main.cpp
        tests/lexer_tests.cpp
        tests/parser_tests.cpp
        tests/names_tests.cpp
        tests/checker_tests.cpp
        tests/web_tests.cpp
        tests/api_tests.cpp
        tests/infrastructure_tests.cpp
        tests/sources_tests.cpp
        tests/grammar_tests.cpp
        tests/complete_tests.cpp
        tests/examples_tests.cpp
        tests/upgrade_tests.cpp
        tests/environments_tests.cpp
        tests/define_tests.cpp)
    target_include_directories(one_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_include_directories(one_tests SYSTEM PRIVATE ${doctest_SOURCE_DIR})
    target_compile_definitions(one_tests PRIVATE UIONE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
    target_link_libraries(one_tests PRIVATE one_warnings md4c)
    add_test(NAME one_tests COMMAND one_tests)
endif()
