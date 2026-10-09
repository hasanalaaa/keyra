# gen_old against another checkout's sources (regen.sh points KEYRA_TREE at a
# git worktree of 3ce814a). Not part of firmware/test/host.
cmake_minimum_required(VERSION 3.16)
project(keyra_upgrade_gen_old CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT KEYRA_TREE)
  message(FATAL_ERROR "set -DKEYRA_TREE=<checkout of the old firmware>")
endif()
if(APPLE AND NOT OPENSSL_ROOT_DIR AND EXISTS /opt/homebrew/opt/openssl@3)
  set(OPENSSL_ROOT_DIR /opt/homebrew/opt/openssl@3)
endif()
find_package(OpenSSL 3 REQUIRED)
add_compile_options(-Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer)
add_link_options(-fsanitize=address,undefined)

set(C ${KEYRA_TREE}/firmware/components)
set(HERE ${CMAKE_CURRENT_SOURCE_DIR}/..)
file(GLOB VAULT_CORE ${C}/keyra_vault/src/core/*.cpp)
file(GLOB FIDO_CORE ${C}/keyra_fido/src/core/*.cpp)
# keyra_vault and keyra_fido both have src/core/platform.hpp: each group of
# sources gets only its own component's internals on the include path.
add_library(old_vault STATIC ${HERE}/platform.cpp ${VAULT_CORE} ${C}/keyra_vault/src/api.cpp
  ${C}/keyra_vault/host_test/host_fakes.cpp)
target_include_directories(old_vault PRIVATE ${C}/keyra_vault/src ${C}/keyra_vault/host_test
  PUBLIC ${C}/keyra_vault/include)
target_link_libraries(old_vault PUBLIC OpenSSL::Crypto)

add_executable(gen_old ${HERE}/gen_old.cpp ${FIDO_CORE} ${C}/keyra_fido/src/vault_store.cpp
  ${C}/keyra_fido/host_test/host_crypto.cpp ${C}/keyra_api/src/activity.cpp)
target_include_directories(gen_old PRIVATE ${HERE} ${C}/keyra_fido/include ${C}/keyra_fido/src
  ${C}/keyra_fido/host_test ${C}/keyra_api/src ${KEYRA_TREE}/firmware/test/host/include)
target_link_libraries(gen_old PRIVATE old_vault)
