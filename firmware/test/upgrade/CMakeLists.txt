# Reading what 3ce814a wrote (and what 0.3.0 leaves after a rollback to it):
# firmware/test/fixtures/v0.2.0-3ce814a, made by regen.sh. Added by firmware/test/host.
if(APPLE AND NOT OPENSSL_ROOT_DIR AND EXISTS /opt/homebrew/opt/openssl@3)
  set(OPENSSL_ROOT_DIR /opt/homebrew/opt/openssl@3)
endif()
find_package(OpenSSL 3 REQUIRED)

set(C ${CMAKE_CURRENT_SOURCE_DIR}/../../components)
# keyra_vault and keyra_fido both have src/core/platform.hpp: each group of
# sources gets only its own component's internals on the include path.
file(GLOB UPGRADE_VAULT_CORE ${C}/keyra_vault/src/core/*.cpp)
add_library(keyra_upgrade_vault STATIC platform.cpp ${UPGRADE_VAULT_CORE} ${C}/keyra_vault/src/api.cpp
  ${C}/keyra_vault/host_test/host_fakes.cpp)
target_include_directories(keyra_upgrade_vault PRIVATE ${C}/keyra_vault/src ${C}/keyra_vault/host_test
  PUBLIC ${C}/keyra_vault/include)
target_link_libraries(keyra_upgrade_vault PUBLIC OpenSSL::Crypto)

# keyra_fido_core comes from keyra_fido/host_test (core sources + host crypto).
add_executable(keyra_upgrade_test upgrade_test.cpp ${C}/keyra_fido/src/vault_store.cpp ${C}/keyra_api/src/activity.cpp)
target_include_directories(keyra_upgrade_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${C}/keyra_fido/host_test
  ${C}/keyra_api/src)
target_compile_definitions(keyra_upgrade_test PRIVATE
  KEYRA_UPGRADE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/v0.2.0-3ce814a")
target_compile_options(keyra_upgrade_test PRIVATE -Werror)
target_link_libraries(keyra_upgrade_test PRIVATE keyra_fido_core keyra_upgrade_vault)
add_test(NAME keyra_upgrade_from_3ce814a COMMAND keyra_upgrade_test)
