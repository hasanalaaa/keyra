# Host tests for keyra_fido's core (no ESP-IDF), built by firmware/test/host:
#   CBOR, CTAPHID framing, CTAP2/U2F (signatures checked with OpenSSL), the
#   device loop, and fido_harness — the core over stdin/stdout for python-fido2
#   (tools/fido_harness.py).
if(APPLE AND NOT OPENSSL_ROOT_DIR AND EXISTS /opt/homebrew/opt/openssl@3)
  set(OPENSSL_ROOT_DIR /opt/homebrew/opt/openssl@3)
endif()
find_package(OpenSSL 3 REQUIRED)

set(FIDO ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_library(keyra_fido_core STATIC
  ${FIDO}/src/core/cbor.cpp ${FIDO}/src/core/ctaphid.cpp ${FIDO}/src/core/ctap.cpp ${FIDO}/src/core/cred.cpp
  ${FIDO}/src/core/touch.cpp ${FIDO}/src/core/device.cpp host_crypto.cpp)
target_include_directories(keyra_fido_core PUBLIC ${FIDO}/src ${FIDO}/include ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(keyra_fido_core PUBLIC -Wall -Wextra -Werror)
target_link_libraries(keyra_fido_core PUBLIC OpenSSL::Crypto)

foreach(t cbor ctaphid ctap device)
  add_executable(keyra_fido_${t}_test ${t}_test.cpp)
  target_link_libraries(keyra_fido_${t}_test PRIVATE keyra_fido_core)
  add_test(NAME keyra_fido_${t} COMMAND keyra_fido_${t}_test)
endforeach()

# The harness stores passkeys in the real vault core (host platform, in RAM).
set(VAULT ${FIDO}/../keyra_vault)
file(GLOB VAULT_CORE ${VAULT}/src/core/*.cpp)
add_executable(fido_harness harness.cpp ${FIDO}/src/vault_store.cpp ${VAULT}/src/api.cpp ${VAULT_CORE}
  ${VAULT}/host_test/host_fakes.cpp ${VAULT}/host_test/host_platform.cpp)
target_include_directories(fido_harness PRIVATE ${VAULT}/include ${VAULT}/src ${VAULT}/host_test)
target_link_libraries(fido_harness PRIVATE keyra_fido_core)
