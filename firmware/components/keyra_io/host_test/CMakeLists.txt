# Host tests for keyra_io's pure parts: button classifier, LED animator.
#   cmake -S . -B build && cmake --build build && ctest --test-dir build
cmake_minimum_required(VERSION 3.16)
project(keyra_io_host_test CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
include_directories(${SRC})

enable_testing()
add_executable(button_test button_test.cpp ${SRC}/button_classifier.cpp)
add_executable(led_test led_test.cpp ${SRC}/led_anim.cpp)
add_test(NAME button COMMAND button_test)
add_test(NAME led COMMAND led_test)
