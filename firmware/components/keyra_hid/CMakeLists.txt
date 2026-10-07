# The layout tables are generated at build time from layouts/layouts.txt (the one source table).
set(KEYRA_LAYOUTS_GEN ${CMAKE_CURRENT_BINARY_DIR}/layouts_gen.cpp)
idf_component_register(
    SRCS "src/hid.cpp" "src/keymap.cpp" "src/typer.cpp" "src/usb_dev.cpp" ${KEYRA_LAYOUTS_GEN}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    PRIV_REQUIRES esp_hw_support esp_hal_usb esp_system esp_timer freertos log soc keyra_ble
)
# After idf_component_register: ESP-IDF first reads this file in script mode,
# where add_custom_command is not allowed (registration returns early there).
idf_build_get_property(python PYTHON)
add_custom_command(
    OUTPUT ${KEYRA_LAYOUTS_GEN}
    COMMAND ${python} ${COMPONENT_DIR}/layouts/gen_layouts.py ${COMPONENT_DIR}/layouts/layouts.txt ${KEYRA_LAYOUTS_GEN}
    DEPENDS ${COMPONENT_DIR}/layouts/layouts.txt ${COMPONENT_DIR}/layouts/gen_layouts.py
    VERBATIM)
# IDF already passes -Wall -Wextra -Werror but then relaxes them globally
# (-Wno-error=…, -Wno-unused-parameter, -Wno-sign-compare); undo that here.
# Plain "-Wall -Wextra -Werror" would be de-duplicated away by CMake.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Werror=extra -Werror=unused-function -Werror=unused-variable
    -Werror=unused-but-set-variable -Werror=deprecated-declarations -Wunused-parameter -Wsign-compare)
