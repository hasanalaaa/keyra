add_executable(keyra_api_actions_test actions_test.cpp ../src/actions.cpp)
add_executable(keyra_api_sessions_test sessions_test.cpp ../src/sessions.cpp)
add_executable(keyra_api_routes_test routes_test.cpp ../src/routes.cpp ../src/clock.cpp ../src/validate.cpp
               ../../keyra_ble/src/policy.cpp)
add_executable(keyra_api_trust_test trust_test.cpp ../src/trust.cpp)
add_executable(keyra_api_target_test target_test.cpp ../src/target.cpp ../../keyra_ble/src/policy.cpp)
add_executable(keyra_api_host_os_test host_os_test.cpp ../src/host_os.cpp ../../keyra_ble/src/policy.cpp)
add_executable(keyra_api_generator_test generator_test.cpp ../src/generator.cpp)
add_executable(keyra_api_health_test health_test.cpp ../src/health.cpp)
add_executable(keyra_api_typetext_test typetext_test.cpp ../src/actions.cpp ../src/validate.cpp)
add_executable(keyra_api_sequence_test sequence_test.cpp ../src/actions.cpp ../src/sequence_run.cpp
               ../../keyra_hid/src/typer.cpp)
# Bond addresses are parsed with keyra::ble::parseAddr; the target (in actions)
# names settings::Output. keyra_ble's stub/ supplies esp_err.h for those headers.
foreach(t actions routes target typetext sequence host_os)
  target_include_directories(keyra_api_${t}_test PRIVATE ../include ../../keyra_net/include ../../keyra_ble/include
                             ../../keyra_ble/host_test/stub)
endforeach()
foreach(t actions sessions routes trust target generator health typetext sequence host_os)
  target_include_directories(keyra_api_${t}_test PRIVATE ../src)
  add_test(NAME keyra_api_${t} COMMAND keyra_api_${t}_test)
endforeach()
# FreeText wipes itself with vault::wipe (the library comes from keyra_vault/host_test).
target_link_libraries(keyra_api_actions_test PRIVATE keyra_vault_host)
target_link_libraries(keyra_api_typetext_test PRIVATE keyra_vault_host keyra_hid_keymap)
# Sequences run through the real typing engine (keyra_hid) over a fake link.
target_link_libraries(keyra_api_sequence_test PRIVATE keyra_vault_host keyra_hid_keymap)
# validate.cpp checks text against the keyboard layouts (keyra_hid's host library).
target_link_libraries(keyra_api_routes_test PRIVATE keyra_hid_keymap)
