# keyra_ble: Bluetooth LE keyboard, HID over GATT on NimBLE (docs/SPEC.md §8.1).
# src/policy.cpp is plain C++ (host-tested in host_test/); the rest is NimBLE glue.
idf_component_register(
    SRCS "src/ble.cpp" "src/gatt.cpp" "src/peer_store.cpp" "src/policy.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_common
    PRIV_REQUIRES bt nvs_flash esp_timer freertos log
)
# IDF already passes -Wall -Wextra -Werror but then relaxes them globally
# (-Wno-error=…, -Wno-unused-parameter, -Wno-sign-compare); undo that here.
# Plain "-Wall -Wextra -Werror" would be de-duplicated away by CMake.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Werror=extra -Werror=unused-function -Werror=unused-variable
    -Werror=unused-but-set-variable -Werror=deprecated-declarations -Wunused-parameter -Wsign-compare)
