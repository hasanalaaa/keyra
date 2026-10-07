# keyra_fido: USB FIDO2/U2F security key (docs/FIDO.md).
# src/core is plain C++ (host-tested in host_test/); src/esp is the device wiring.
idf_component_register(
  SRCS
    "src/core/cbor.cpp"
    "src/core/ctap.cpp"
    "src/core/ctaphid.cpp"
    "src/core/cred.cpp"
    "src/core/device.cpp"
    "src/core/touch.cpp"
    "src/vault_store.cpp"
    "src/esp/fido_task.cpp"
    "src/esp/psa_crypto.cpp"
  INCLUDE_DIRS "include"
  PRIV_INCLUDE_DIRS "src"
  PRIV_REQUIRES keyra_hid keyra_vault mbedtls nvs_flash esp_timer freertos log)

# Like keyra_hid: undo IDF's global relaxations so warnings fail the build.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Werror=extra -Werror=unused-function -Werror=unused-variable
    -Werror=unused-but-set-variable -Werror=deprecated-declarations -Wunused-parameter -Wsign-compare)
