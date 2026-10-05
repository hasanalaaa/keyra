# Host test runner (SPEC §7): plain C++, no ESP-IDF.
#   cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
cmake_minimum_required(VERSION 3.16)
project(keyra_host_tests C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KEYRA_SANITIZE "Build host tests with ASan + UBSan" ON)
add_compile_options(-Wall -Wextra)
if(KEYRA_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
  add_link_options(-fsanitize=address,undefined)
endif()

enable_testing()
# keyra_test.hpp: tiny CHECK macros any component's host_test may use.
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

file(GLOB KEYRA_COMPONENTS LIST_DIRECTORIES true ${CMAKE_CURRENT_SOURCE_DIR}/../../components/*)
foreach(dir ${KEYRA_COMPONENTS})
  if(EXISTS ${dir}/host_test/CMakeLists.txt)
    get_filename_component(name ${dir} NAME)
    add_subdirectory(${dir}/host_test ${CMAKE_CURRENT_BINARY_DIR}/${name})
  endif()
endforeach()
