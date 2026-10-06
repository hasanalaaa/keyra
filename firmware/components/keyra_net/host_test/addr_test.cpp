// Interface decisions (SPEC §8.2): which requests came over Keyra's own Wi-Fi,
// who the captive DNS answers, and the scan list the picker shows.
#include "addr.hpp"
#include "keyra_test.hpp"
#include "scan_list.hpp"

using namespace keyra::net;

namespace {

void viaFromLocalAddress() {
  CHECK(classify(ipv4(192, 168, 4, 1)) == Via::Ap);
  CHECK(classify(ipv4(192, 168, 1, 42)) == Via::Home);
  // A home LAN that happens to use 192.168.4.0/24 is still the home side.
  CHECK(classify(ipv4(192, 168, 4, 77)) == Via::Home);
  CHECK(classify(0) == Via::Home);  // unknown: the side with more checks
}

void v4MappedAddresses() {
  const uint8_t mapped[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 192, 168, 4, 1};
  CHECK_EQ(fromV4Mapped(mapped), kApIp);
  const uint8_t linkLocal[16] = {0xFE, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 192, 168, 4, 1};
  CHECK_EQ(fromV4Mapped(linkLocal), 0u);
  const uint8_t compat[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 4, 1};
  CHECK_EQ(fromV4Mapped(compat), 0u);
  CHECK(classify(fromV4Mapped(linkLocal)) == Via::Home);
}

void dnsAnswersOnlyTheApSubnet() {
  CHECK(inApSubnet(ipv4(192, 168, 4, 2)));
  CHECK(inApSubnet(ipv4(192, 168, 4, 254)));
  CHECK(!inApSubnet(ipv4(192, 168, 4, 1)));  // ourselves
  CHECK(!inApSubnet(ipv4(192, 168, 1, 20)));
  CHECK(!inApSubnet(ipv4(10, 0, 0, 5)));
  CHECK(toString(ipv4(192, 168, 1, 42)) == "192.168.1.42");
}

void scanListIsTidy() {
  using scanlist::Record;
  using scanlist::Security;
  const std::vector<Record> raw = {
      {"Home", -70, Security::Psk, 1},
      {"", -30, Security::Psk, 6},                // hidden
      {"Cafe", -50, Security::Open, 11},          // listed, not joinable
      {"Home", -48, Security::Psk, 6},            // stronger mesh node wins
      {"Office", -60, Security::Other, 3},        // enterprise: never joinable
      {"Bad\x01Name", -40, Security::Psk, 2},     // control character
      {"Neighbour", -85, Security::Psk, 9},
  };
  const auto list = scanlist::tidy(raw);
  CHECK_EQ(list.size(), 3u);
  CHECK(list[0].ssid == "Home" && list[0].rssi == -48 && list[0].channel == 6 && list[0].secure);
  CHECK(list[1].ssid == "Cafe" && !list[1].secure);
  CHECK(list[2].ssid == "Neighbour");

  std::vector<Record> many;
  for (int i = 0; i < 30; ++i) many.push_back({"n" + std::to_string(i), -90 + i, Security::Psk, 1});
  const auto capped = scanlist::tidy(many);
  CHECK_EQ(capped.size(), scanlist::kMaxItems);
  CHECK(capped.front().ssid == "n29");
}

}  // namespace

int main() {
  viaFromLocalAddress();
  v4MappedAddresses();
  dnsAnswersOnlyTheApSubnet();
  scanListIsTidy();
  return KEYRA_TEST_RESULT();
}
