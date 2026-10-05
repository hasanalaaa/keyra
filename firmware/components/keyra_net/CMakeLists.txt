idf_component_register(
    SRCS "src/net.cpp" "src/dns_server.cpp" "src/dns_packet.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_common
    PRIV_REQUIRES esp_wifi esp_netif esp_event lwip log freertos
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
