cmake_minimum_required(VERSION 3.22)

# Keep sdkconfig inside each build dir: the dev and release profiles are built
# side by side (build/, build-release/) and must not overwrite each other.
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
set(PROJECT_VER "0.2.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(keyra)
