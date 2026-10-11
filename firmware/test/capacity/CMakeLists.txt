# Capacity bench (docs/research/CAPACITY.md): the real vault core and entry
# codec over the real littlefs that esp_littlefs ships, configured as on the
# device, on a RAM flash that counts every read, program and erase.
# Not a test: run it by hand.
#   cmake -S firmware/test/capacity -B /tmp/cap -DCMAKE_BUILD_TYPE=Release
#   cmake --build /tmp/cap && /tmp/cap/keyra_capacity all
# littlefs comes from managed_components, which the first idf.py build fetches.
cmake_minimum_required(VERSION 3.16)
project(keyra_capacity C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(FW ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(LFS_DIR ${FW}/managed_components/joltwallet__littlefs/src/littlefs CACHE PATH "littlefs sources")
if(NOT EXISTS ${LFS_DIR}/lfs.c)
  message(FATAL_ERROR "littlefs not found at ${LFS_DIR}: run idf.py build once (or pass -DLFS_DIR=)")
endif()

if(APPLE AND NOT OPENSSL_ROOT_DIR AND EXISTS /opt/homebrew/opt/openssl@3)
  set(OPENSSL_ROOT_DIR /opt/homebrew/opt/openssl@3)
endif()
find_package(OpenSSL 3 REQUIRED)

add_library(lfs STATIC ${LFS_DIR}/lfs.c ${LFS_DIR}/lfs_util.c)
target_include_directories(lfs PUBLIC ${LFS_DIR})
target_compile_definitions(lfs PUBLIC LFS_NO_DEBUG LFS_NO_WARN LFS_NO_ERROR)

set(V ${FW}/components/keyra_vault)
file(GLOB VAULT_CORE ${V}/src/core/*.cpp)
add_executable(keyra_capacity capacity_bench.cpp ${VAULT_CORE} ${V}/src/api.cpp ${V}/host_test/host_fakes.cpp)
target_include_directories(keyra_capacity PRIVATE ${V}/include ${V}/src ${V}/host_test)
target_compile_options(keyra_capacity PRIVATE -Wall -Wextra)
target_link_libraries(keyra_capacity PRIVATE lfs OpenSSL::Crypto)
