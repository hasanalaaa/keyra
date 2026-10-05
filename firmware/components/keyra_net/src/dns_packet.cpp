#include "dns_packet.hpp"

#include <cstring>

namespace keyra::net::dns {
namespace {

constexpr size_t kHeader = 12;
constexpr uint8_t kRcodeFormErr = 1, kRcodeNotImp = 4;
// Short TTL: a phone that leaves Keyra's Wi-Fi must not keep resolving real
// hosts (e.g. captive.apple.com) to 192.168.4.1 on its next network.
constexpr uint32_t kTtlSeconds = 10;

uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

size_t headerOnly(const uint8_t* q, uint8_t rcode, uint8_t* out, size_t cap) {
  if (cap < kHeader) return 0;
  std::memset(out, 0, kHeader);
  out[0] = q[0];
  out[1] = q[1];
  out[2] = static_cast<uint8_t>(0x80 | (q[2] & 0x79));  // QR + echoed opcode/RD
  out[3] = static_cast<uint8_t>(0x80 | rcode);          // RA + rcode
  return kHeader;
}

}  // namespace

size_t reply(const uint8_t* q, size_t len, const std::array<uint8_t, 4>& ip, uint8_t* out,
             size_t cap) {
  if (len < kHeader || (q[2] & 0x80)) return 0;
  const uint8_t opcode = (q[2] >> 3) & 0x0F;
  if (opcode != 0) return headerOnly(q, kRcodeNotImp, out, cap);
  if (be16(q + 4) != 1) return headerOnly(q, kRcodeFormErr, out, cap);

  size_t pos = kHeader;
  for (;;) {
    if (pos >= len) return 0;
    const uint8_t label = q[pos];
    if (label == 0) {
      ++pos;
      break;
    }
    if (label & 0xC0) return 0;  // compression pointers are not legal in a lone question
    pos += 1 + label;
    if (pos - kHeader > 255) return 0;
  }
  if (pos + 4 > len) return 0;
  const uint16_t qtype = be16(q + pos), qclass = be16(q + pos + 2);
  const size_t questionEnd = pos + 4;
  const bool answerA = qtype == 1 && qclass == 1;
  const size_t total = questionEnd + (answerA ? 16 : 0);
  if (total > cap) return 0;

  std::memcpy(out, q, questionEnd);
  out[2] = static_cast<uint8_t>(0x80 | (q[2] & 0x79) | 0x04);  // QR + AA, echo opcode/RD
  out[3] = 0x80;                                               // RA, NOERROR
  out[4] = 0;
  out[5] = 1;  // QDCOUNT
  out[6] = 0;
  out[7] = answerA ? 1 : 0;  // ANCOUNT
  std::memset(out + 8, 0, 4);  // NSCOUNT, ARCOUNT (any EDNS OPT record is dropped)
  if (!answerA) return questionEnd;

  uint8_t* a = out + questionEnd;
  const uint8_t answer[] = {0xC0, 0x0C,  // name: pointer to the question
                            0x00, 0x01, 0x00, 0x01,
                            static_cast<uint8_t>(kTtlSeconds >> 24), static_cast<uint8_t>(kTtlSeconds >> 16),
                            static_cast<uint8_t>(kTtlSeconds >> 8), static_cast<uint8_t>(kTtlSeconds),
                            0x00, 0x04, ip[0], ip[1], ip[2], ip[3]};
  std::memcpy(a, answer, sizeof answer);
  return total;
}

}  // namespace keyra::net::dns
