#include <array>
#include <cstdint>
#include <vector>

#include "dns_packet.hpp"
#include "keyra_test.hpp"

using keyra::net::dns::reply;

namespace {

const std::array<uint8_t, 4> kIp = {192, 168, 4, 1};

std::vector<uint8_t> query(const char* name, uint16_t qtype, bool withEdns = false) {
  std::vector<uint8_t> q = {0xAB, 0xCD, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, static_cast<uint8_t>(withEdns ? 1 : 0)};
  const char* p = name;
  while (*p) {
    const char* dot = p;
    while (*dot && *dot != '.') ++dot;
    q.push_back(static_cast<uint8_t>(dot - p));
    q.insert(q.end(), p, dot);
    p = *dot ? dot + 1 : dot;
  }
  q.push_back(0);
  q.insert(q.end(), {static_cast<uint8_t>(qtype >> 8), static_cast<uint8_t>(qtype), 0, 1});
  if (withEdns) q.insert(q.end(), {0, 0, 41, 0x10, 0, 0, 0, 0, 0, 0, 0});
  return q;
}

void answersAQuery() {
  auto q = query("captive.apple.com", 1);
  uint8_t out[512];
  const size_t n = reply(q.data(), q.size(), kIp, out, sizeof out);
  CHECK_EQ(n, q.size() + 16);
  CHECK_EQ(out[0], 0xAB);
  CHECK_EQ(out[1], 0xCD);
  CHECK(out[2] & 0x80);         // QR
  CHECK(out[2] & 0x04);         // AA
  CHECK(out[2] & 0x01);         // RD echoed
  CHECK_EQ(out[3] & 0x0F, 0);   // NOERROR
  CHECK_EQ(out[7], 1);          // ANCOUNT
  CHECK_EQ(out[n - 4], 192);
  CHECK_EQ(out[n - 3], 168);
  CHECK_EQ(out[n - 2], 4);
  CHECK_EQ(out[n - 1], 1);
  CHECK_EQ(out[q.size()], 0xC0);  // name pointer to question
}

void aaaaGetsEmptyNoError() {
  auto q = query("keyra.local", 28);
  uint8_t out[512];
  const size_t n = reply(q.data(), q.size(), kIp, out, sizeof out);
  CHECK_EQ(n, q.size());
  CHECK_EQ(out[3] & 0x0F, 0);
  CHECK_EQ(out[7], 0);
}

void ednsRecordIsDropped() {
  auto q = query("example.com", 1, true);
  uint8_t out[512];
  const size_t n = reply(q.data(), q.size(), kIp, out, sizeof out);
  CHECK_EQ(n, q.size() - 11 + 16);
  CHECK_EQ(out[11], 0);  // ARCOUNT cleared
  CHECK_EQ(out[n - 1], 1);
}

void rejectsMalformed() {
  uint8_t out[512];
  const uint8_t tiny[5] = {1, 2, 3, 4, 5};
  CHECK_EQ(reply(tiny, sizeof tiny, kIp, out, sizeof out), 0u);

  auto resp = query("a.b", 1);
  resp[2] |= 0x80;  // already a response: never answer (avoids reflection loops)
  CHECK_EQ(reply(resp.data(), resp.size(), kIp, out, sizeof out), 0u);

  auto truncated = query("example.com", 1);
  truncated.resize(truncated.size() - 3);
  CHECK_EQ(reply(truncated.data(), truncated.size(), kIp, out, sizeof out), 0u);

  auto overrun = query("example.com", 1);
  overrun[12] = 60;  // label runs past the packet
  CHECK_EQ(reply(overrun.data(), overrun.size(), kIp, out, sizeof out), 0u);

  auto pointer = query("example.com", 1);
  pointer[12] = 0xC0;
  CHECK_EQ(reply(pointer.data(), pointer.size(), kIp, out, sizeof out), 0u);

  auto q = query("example.com", 1);
  CHECK_EQ(reply(q.data(), q.size(), kIp, out, q.size()), 0u);  // no room for the answer
}

void unsupportedShapesGetHeaderOnlyErrors() {
  uint8_t out[512];
  auto two = query("a.b", 1);
  two[5] = 2;
  CHECK_EQ(reply(two.data(), two.size(), kIp, out, sizeof out), 12u);
  CHECK_EQ(out[3] & 0x0F, 1);  // FORMERR

  auto notify = query("a.b", 1);
  notify[2] = 4 << 3;  // opcode NOTIFY
  CHECK_EQ(reply(notify.data(), notify.size(), kIp, out, sizeof out), 12u);
  CHECK_EQ(out[3] & 0x0F, 4);  // NOTIMP
}

}  // namespace

int main() {
  answersAQuery();
  aaaaGetsEmptyNoError();
  ednsRecordIsDropped();
  rejectsMalformed();
  unsupportedShapesGetHeaderOnlyErrors();
  return KEYRA_TEST_RESULT();
}
