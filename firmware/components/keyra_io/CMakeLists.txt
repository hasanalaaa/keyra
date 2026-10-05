idf_component_register(
    SRCS "src/io.cpp" "src/button_classifier.cpp" "src/led_anim.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES freertos
    PRIV_REQUIRES esp_driver_gpio esp_timer log
)
# IDF already passes -Wall -Wextra -Werror but then relaxes them globally
# (-Wno-error=…, -Wno-unused-parameter, -Wno-sign-compare); undo that here.
# Plain "-Wall -Wextra -Werror" would be de-duplicated away by CMake.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Werror=extra -Werror=unused-function -Werror=unused-variable
    -Werror=unused-but-set-variable -Werror=deprecated-declarations -Wunused-parameter -Wsign-compare)
