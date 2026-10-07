add_executable(keyra_ble_policy_test policy_test.cpp ../src/policy.cpp)
# stub/ supplies esp_err.h; keyra_hid/src holds the USB report map the BLE one must equal.
target_include_directories(keyra_ble_policy_test PRIVATE ../src ../include stub ../../keyra_hid/src)
add_test(NAME keyra_ble_policy COMMAND keyra_ble_policy_test)
