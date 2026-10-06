add_executable(keyra_api_actions_test actions_test.cpp ../src/actions.cpp)
add_executable(keyra_api_sessions_test sessions_test.cpp ../src/sessions.cpp)
add_executable(keyra_api_routes_test routes_test.cpp ../src/routes.cpp ../src/clock.cpp ../src/validate.cpp
               ../../keyra_ble/src/policy.cpp)
add_executable(keyra_api_trust_test trust_test.cpp ../src/trust.cpp)
add_executable(keyra_api_target_test target_test.cpp ../src/target.cpp ../../keyra_ble/src/policy.cpp)
# Bond addresses are parsed with keyra::ble::parseAddr; the target (in actions)
# names settings::Output. keyra_ble's stub/ supplies esp_err.h for those headers.
foreach(t actions routes target)
  target_include_directories(keyra_api_${t}_test PRIVATE ../include ../../keyra_net/include ../../keyra_ble/include
                             ../../keyra_ble/host_test/stub)
endforeach()
foreach(t actions sessions routes trust target)
  target_include_directories(keyra_api_${t}_test PRIVATE ../src)
  add_test(NAME keyra_api_${t} COMMAND keyra_api_${t}_test)
endforeach()
