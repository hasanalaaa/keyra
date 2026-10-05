idf_component_register(
    SRCS "src/hid.cpp" "src/keymap.cpp" "src/typer.cpp" "src/usb_dev.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    PRIV_REQUIRES esp_hw_support esp_hal_usb esp_system esp_timer freertos log soc
)
# IDF already passes -Wall -Wextra -Werror but then relaxes them globally
# (-Wno-error=…, -Wno-unused-parameter, -Wno-sign-compare); undo that here.
# Plain "-Wall -Wextra -Werror" would be de-duplicated away by CMake.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Werror=extra -Werror=unused-function -Werror=unused-variable
    -Werror=unused-but-set-variable -Werror=deprecated-declarations -Wunused-parameter -Wsign-compare)
