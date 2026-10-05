# Host tests for keyra_hid's pure parts: keymap, typing engine, descriptors.
#   cmake -S . -B build && cmake --build build && ctest --test-dir build
cmake_minimum_required(VERSION 3.16)
project(keyra_hid_host_test CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
include_directories(${SRC} ${CMAKE_CURRENT_SOURCE_DIR}/../include)

enable_testing()
add_executable(keymap_test keymap_test.cpp ${SRC}/keymap.cpp)
add_executable(typer_test typer_test.cpp ${SRC}/typer.cpp ${SRC}/keymap.cpp)
add_executable(desc_test desc_test.cpp)
add_test(NAME keymap COMMAND keymap_test)
add_test(NAME typer COMMAND typer_test)
add_test(NAME descriptors COMMAND desc_test)
