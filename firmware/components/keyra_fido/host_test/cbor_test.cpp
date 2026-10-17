// CBOR encoder/decoder: RFC 8949 Appendix A vectors and malformed input.
#include <string>
#include <vector>

#include "core/cbor.hpp"
#include "keyra_test.hpp"

using namespace keyra::fido::cbor;

namespace {

std::vector<uint8_t> hex(const std::string& h) {
  std::vector<uint8_t> v;
  for (size_t i = 0; i + 1 < h.size(); i += 2) v.push_back(static_cast<uint8_t>(std::stoi(h.substr(i, 2), nullptr, 16)));
  return v;
}

bool dec(const std::string& h, Value& v) {
  const auto b = hex(h);
  return decode(b.data(), b.size(), v);
}

void encodeVectors() {
  struct {
    int64_t v;
    const char* hex;
  } ints[] = {{0, "00"},           {1, "01"},           {10, "0a"},         {23, "17"},
              {24, "1818"},        {25, "1819"},        {100, "1864"},      {1000, "1903e8"},
              {1000000, "1a000f4240"}, {1000000000000, "1b000000e8d4a51000"}, {-1, "20"},
              {-10, "29"},         {-100, "3863"},      {-1000, "3903e7"}};
  for (const auto& t : ints) {
    Writer w;
    w.integer(t.v);
    CHECK(w.out == hex(t.hex));
    Value v;
    int64_t back = 0;
    CHECK(dec(t.hex, v) && v.asInt(back) && back == t.v);
  }
  Writer w;
  w.uint(18446744073709551615ull);
  CHECK(w.out == hex("1bffffffffffffffff"));

  Writer s;
  s.text("IETF");
  CHECK(s.out == hex("6449455446"));
  Writer b;
  const uint8_t four[] = {1, 2, 3, 4};
  b.bytes(four, 4);
  CHECK(b.out == hex("4401020304"));
  Writer e;
  e.text("");
  CHECK(e.out == hex("60"));

  // {"a": 1, "b": [2, 3]}
  Writer m;
  m.map(2);
  m.text("a"), m.uint(1);
  m.text("b"), m.array(2), m.uint(2), m.uint(3);
  CHECK(m.out == hex("a26161016162820203"));
  Writer t;
  t.boolean(true), t.boolean(false);
  CHECK(t.out == hex("f5f4"));
}

void decodeStructures() {
  Value v;
  CHECK(dec("a26161016162820203", v) && v.type == Value::Type::Map && v.entries.size() == 2);
  const Value* b = v.find("b");
  CHECK(b && b->type == Value::Type::Array && b->items.size() == 2 && b->items[1].u == 3);
  CHECK(v.find("c") == nullptr && v.find(1) == nullptr);
  // {1: 2, 3: 4} with integer keys
  CHECK(dec("a201020304", v) && v.find(3) && v.find(3)->u == 4);
  // "ü" and "水" (UTF-8)
  CHECK(dec("62c3bc", v) && v.type == Value::Type::Text && v.text() == "\xc3\xbc");
  CHECK(dec("63e6b0b4", v) && v.text() == "\xe6\xb0\xb4");
  CHECK(dec("f6", v) && v.type == Value::Type::Null);
  // -2^64 does not fit int64 but decodes as a Neg
  int64_t x;
  CHECK(dec("3bffffffffffffffff", v) && v.type == Value::Type::Neg && !v.asInt(x));
}

void malformed() {
  Value v;
  const char* bad[] = {
      "",                   // nothing
      "18",                 // truncated argument
      "4401020",            // odd hex → 3 bytes, string of 4 truncated
      "440102",             // byte string shorter than its length
      "5f42010243030405ff", // indefinite-length byte string
      "9fff",               // indefinite array
      "bf6161ff",           // indefinite map
      "c11a514b67b0",       // tag
      "f93c00",             // half float
      "fb3ff199999999999a", // double
      "f7",                 // undefined
      "1c",                 // reserved additional info
      "0001",               // trailing byte
      "62c328",             // invalid UTF-8 in text
      "63eda080",           // UTF-16 surrogate encoded in UTF-8
      "a2616101616102",     // duplicate map key "a"
      "9a7fffffff",         // array count far beyond the input
      "818181818181818181818100",  // nesting deeper than kMaxDepth
  };
  for (const char* h : bad) CHECK(!dec(h, v));
  CHECK(dec("8181818181818100", v));  // depth 7 is fine
}

// F9: sizes a host declares are checked against the input and a value budget
// before anything is allocated.
void declaredSizes() {
  Value v;
  // 7000 one-byte items: well-formed, inside a CTAPHID message, but over budget.
  std::vector<uint8_t> big = {0x99, 0x1B, 0x58};
  big.resize(3 + 7000, 0x00);
  CHECK(!decode(big.data(), big.size(), v));
  // The same for a map of 3500 pairs.
  std::vector<uint8_t> map = {0xB9, 0x0D, 0xAC};
  for (int k = 0; k < 3500; ++k) map.push_back(0x00), map.push_back(0x00);
  CHECK(!decode(map.data(), map.size(), v));
  // Nested arrays each declaring nearly the whole input: refused at the first.
  std::vector<uint8_t> nest;
  for (int d = 0; d < 8; ++d) nest.insert(nest.end(), {0x99, 0x1D, 0x00});
  nest.resize(7600, 0x00);
  CHECK(!decode(nest.data(), nest.size(), v));
  // Counts far beyond the input, in every argument width.
  for (const char* h : {"9b00000000ffffffff", "9bffffffffffffffff", "bb7fffffffffffffff", "5bffffffffffffffff",
                        "7a7fffffff", "b9ffff"})
    CHECK(!dec(h, v));
  // The budget is for the whole item, not per container.
  std::vector<uint8_t> two = {0x82, 0x99, 0x01, 0x2C};  // [ [300 x 0], [300 x 0] ]
  two.resize(two.size() + 300, 0x00);
  two.insert(two.end(), {0x99, 0x01, 0x2C});
  two.resize(two.size() + 300, 0x00);
  CHECK(!decode(two.data(), two.size(), v));
  // Within the budget it still decodes.
  std::vector<uint8_t> ok = {0x99, 0x01, 0xFF};  // 511 items + the array itself
  ok.resize(3 + 511, 0x00);
  CHECK(decode(ok.data(), ok.size(), v) && v.items.size() == 511);
  ok[1] = 0x02, ok[2] = 0x00, ok.push_back(0x00);  // 512 items: one more than the budget leaves
  CHECK(!decode(ok.data(), ok.size(), v));
}

}  // namespace

int main() {
  encodeVectors();
  decodeStructures();
  malformed();
  declaredSizes();
  return KEYRA_TEST_RESULT();
}
