add_executable(keyra_net_dns_test dns_packet_test.cpp ../src/dns_packet.cpp)
target_include_directories(keyra_net_dns_test PRIVATE ../src)
add_test(NAME keyra_net_dns COMMAND keyra_net_dns_test)
