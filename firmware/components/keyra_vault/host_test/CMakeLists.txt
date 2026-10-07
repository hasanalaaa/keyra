# Host tests for the vault core (no ESP-IDF):
#   cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
# Also usable via add_subdirectory() from firmware/test/host.
cmake_minimum_required(VERSION 3.16)
project(keyra_vault_host_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE AND NOT OPENSSL_ROOT_DIR AND EXISTS /opt/homebrew/opt/openssl@3)
  set(OPENSSL_ROOT_DIR /opt/homebrew/opt/openssl@3)
endif()
find_package(OpenSSL 3 REQUIRED)
enable_testing()

set(VAULT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_library(keyra_vault_host STATIC
  ${VAULT_DIR}/src/api.cpp
  ${VAULT_DIR}/src/core/backup_format.cpp
  ${VAULT_DIR}/src/core/entry_codec.cpp
  ${VAULT_DIR}/src/core/json.cpp
  ${VAULT_DIR}/src/core/secure_buf.cpp
  ${VAULT_DIR}/src/core/sequence.cpp
  ${VAULT_DIR}/src/core/text.cpp
  ${VAULT_DIR}/src/core/totp_core.cpp
  ${VAULT_DIR}/src/core/vault_core.cpp
  host_fakes.cpp
  host_platform.cpp)
target_include_directories(keyra_vault_host PUBLIC ${VAULT_DIR}/include ${VAULT_DIR}/src
                                                   ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(keyra_vault_host PUBLIC -Wall -Wextra -Werror)
target_link_libraries(keyra_vault_host PUBLIC OpenSSL::Crypto)

foreach(t vault backup totp formats sequence)
  add_executable(keyra_vault_test_${t} test_${t}.cpp)
  target_link_libraries(keyra_vault_test_${t} PRIVATE keyra_vault_host)
  add_test(NAME keyra_vault_${t} COMMAND keyra_vault_test_${t})
endforeach()
