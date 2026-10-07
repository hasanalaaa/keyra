# Host tests for keyra_hid's pure parts: keymap/layouts, typing engine, descriptors.
#   cmake -S . -B build && cmake --build build && ctest --test-dir build
cmake_minimum_required(VERSION 3.16)
project(keyra_hid_host_test CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set(LAYOUTS ${CMAKE_CURRENT_SOURCE_DIR}/../layouts)
set(LAYOUTS_GEN ${CMAKE_CURRENT_BINARY_DIR}/layouts_gen.cpp)
add_custom_command(
    OUTPUT ${LAYOUTS_GEN}
    COMMAND Python3::Interpreter ${LAYOUTS}/gen_layouts.py ${LAYOUTS}/layouts.txt ${LAYOUTS_GEN}
    DEPENDS ${LAYOUTS}/layouts.txt ${LAYOUTS}/gen_layouts.py
    VERBATIM)
add_library(keyra_hid_keymap STATIC ${SRC}/keymap.cpp ${LAYOUTS_GEN})
target_include_directories(keyra_hid_keymap PUBLIC ${SRC} ${CMAKE_CURRENT_SOURCE_DIR}/../include)

enable_testing()
add_executable(keymap_test keymap_test.cpp)
target_link_libraries(keymap_test PRIVATE keyra_hid_keymap)
add_executable(layouts_test layouts_test.cpp)
target_link_libraries(layouts_test PRIVATE keyra_hid_keymap)
add_executable(typer_test typer_test.cpp ${SRC}/typer.cpp)
target_link_libraries(typer_test PRIVATE keyra_hid_keymap)
add_executable(desc_test desc_test.cpp)
target_include_directories(desc_test PRIVATE ${SRC} ${CMAKE_CURRENT_SOURCE_DIR}/../include)
add_test(NAME keymap COMMAND keymap_test)
add_test(NAME layouts COMMAND layouts_test)
add_test(NAME typer COMMAND typer_test)
add_test(NAME descriptors COMMAND desc_test)
