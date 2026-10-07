# keyra_vault: encrypted storage, TOTP and backup (docs/SPEC.md §4.1, §6).
# src/core is plain C++ (host-tested in host_test/); src/esp holds the ESP-IDF adapters.
idf_component_register(
  SRCS
    "src/api.cpp"
    "src/core/backup_format.cpp"
    "src/core/entry_codec.cpp"
    "src/core/json.cpp"
    "src/core/secure_buf.cpp"
    "src/core/text.cpp"
    "src/core/totp_core.cpp"
    "src/core/vault_core.cpp"
    "src/core/vault_passkeys.cpp"
    "src/esp/esp_platform.cpp"
    "src/esp/littlefs_storage.cpp"
    "src/esp/psa_crypto.cpp"
  INCLUDE_DIRS "include"
  PRIV_INCLUDE_DIRS "src"
  PRIV_REQUIRES mbedtls nvs_flash esp_partition esp_timer heap joltwallet__littlefs)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
