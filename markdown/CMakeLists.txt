cmake_minimum_required(VERSION 3.20)

project(md
    VERSION 0.1.0
    DESCRIPTION "A terminal Markdown renderer"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)

# libmarkdown: one header, nothing to compile.
add_library(markdown INTERFACE)
target_include_directories(markdown INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(markdown INTERFACE cxx_std_23)

add_executable(md src/main.cpp)
target_link_libraries(md PRIVATE markdown)
target_compile_options(md PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(layout_test test/layout_test.cpp)
target_link_libraries(layout_test PRIVATE markdown)
target_compile_options(layout_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME layout
    COMMAND layout_test ${CMAKE_CURRENT_SOURCE_DIR}/test/sample.md)

install(TARGETS md RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES include/markdown.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
