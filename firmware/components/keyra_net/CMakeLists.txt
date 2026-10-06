idf_component_register(
    SRCS "src/net.cpp" "src/dns_server.cpp" "src/dns_packet.cpp" "src/link.cpp" "src/addr.cpp"
         "src/scan_list.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_common
    PRIV_REQUIRES esp_wifi esp_netif esp_event esp_timer lwip log freertos
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
