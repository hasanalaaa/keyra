idf_component_register(
    SRCS "src/api.cpp" "src/server.cpp" "src/handlers.cpp" "src/http.cpp" "src/json.cpp"
         "src/runtime.cpp" "src/actions.cpp" "src/sessions.cpp" "src/routes.cpp"
         "src/clock.cpp" "src/validate.cpp" "src/settings.cpp" "src/target.cpp"
         "src/trust.cpp" "src/trusted.cpp" "src/handlers_net.cpp" "src/handlers_fido.cpp"
         "src/handlers_gen.cpp" "src/generator.cpp" "src/handlers_kbd.cpp" "src/sequence_run.cpp" "src/host_os.cpp"
         "src/handlers_protect.cpp" "src/health.cpp" "src/activity.cpp" "src/activity_log.cpp"
         "src/version.cpp" "src/handlers_update.cpp" "src/tokens.cpp" "src/handlers_agent.cpp" "src/host_match.cpp"
         "src/companion.cpp"
         "src/tags.cpp" "src/handlers_tags.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_common keyra_net
    PRIV_REQUIRES esp_http_server esp_http_client esp_timer esp_hw_support esp_app_format app_update nvs_flash heap mbedtls
                  log freertos keyra_vault keyra_hid keyra_io keyra_ble keyra_fido
    # Built by web/ (`npm run build`); committed so the firmware builds without Node.
    EMBED_FILES "www/index.html.gz" "www/manifest.webmanifest" "www/icon-192.png" "www/icon-512.png"
                "www/apple-touch-icon.png" "www/favicon.svg" "www/tap.html"
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
