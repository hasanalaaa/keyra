idf_component_register(
    SRCS "main.cpp"
    PRIV_REQUIRES nvs_flash log keyra_io keyra_vault keyra_hid keyra_net keyra_api
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
