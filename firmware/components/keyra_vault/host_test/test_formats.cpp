// Building blocks: UTF-8 validation, base64, JSON reader/writer, entry codec.
#include <cstring>

#include "check.hpp"
#include "core/entry_codec.hpp"
#include "core/json.hpp"
#include "core/text.hpp"
#include "rig.hpp"

using namespace keyra::vault;

static std::string str(const SecureString& s) { return std::string(s.data(), s.size()); }

TEST(utf8) {
  CHECK(text::validUtf8(""));
  CHECK(text::validUtf8("ascii"));
  CHECK(text::validUtf8("مرحبا 🙂"));
  CHECK(!text::validUtf8("\x80"));
  CHECK(!text::validUtf8("\xC0\xAF"));          // overlong '/'
  CHECK(!text::validUtf8("\xE0\x80\xAF"));      // overlong
  CHECK(!text::validUtf8("\xED\xA0\x80"));      // surrogate
  CHECK(!text::validUtf8("\xF4\x90\x80\x80"));  // > U+10FFFF
  CHECK(!text::validUtf8("\xE2\x82"));          // truncated
  CHECK(text::codePoints("مرحبا") == 5);
  CHECK(text::codePoints("a🙂b") == 3);
}

TEST(base64) {
  const char* plain[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
  const char* enc[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
  for (int i = 0; i < 7; ++i) {
    std::string p = plain[i];
    CHECK(text::base64Encode(reinterpret_cast<const uint8_t*>(p.data()), p.size()) == enc[i]);
    std::vector<uint8_t> out;
    CHECK(text::base64Decode(enc[i], out) && std::string(out.begin(), out.end()) == p);
  }
  std::vector<uint8_t> out;
  CHECK(!text::base64Decode("Zg=", out));
  CHECK(!text::base64Decode("Z===", out));
  CHECK(!text::base64Decode("Zg=a", out));
  CHECK(!text::base64Decode("Zg==Zg==", out));  // padding mid-stream
  CHECK(!text::base64Decode("Zm9v YmFy", out));
  CHECK(!text::base64Decode("Zm9-", out));
}

TEST(json_reader) {
  json::Value v;
  const std::string doc =
      " { \"a\" : [1, -2, 0, true, false, null, \"x\\\"y\\\\z\\/\\n\\t\\u00e9\\u0645\\ud83d\\ude42\"],"
      " \"b\": {\"c\": 9223372036854775807, \"d\": -9223372036854775808} } ";
  CHECK(json::parse(doc.data(), doc.size(), v));
  const json::Value* a = v.find("a");
  CHECK(a && a->type == json::Value::Type::Array && a->items.size() == 7);
  CHECK(a->items[1].i == -2 && a->items[3].b && !a->items[4].b);
  CHECK(a->items[5].type == json::Value::Type::Null);
  CHECK(str(a->items[6].s) == "x\"y\\z/\n\té" "م" "🙂");
  CHECK(v.find("b")->find("c")->i == INT64_MAX);
  CHECK(v.find("b")->find("d")->i == INT64_MIN);
  CHECK(!v.find("zzz"));

  const char* bad[] = {
      "",          "{",          "[1,]",         "{\"a\":1,}",        "01",
      "1.5",       "1e3",        "9223372036854775808", "\"\\x\"", "\"a\nb\"",
      "\"\\ud83d\"", "\"\\ude42\"", "[1] 2",       "tru",               "{1:2}",
      "[[[[[[[[[[1]]]]]]]]]]",
  };
  for (const char* b : bad) CHECK(!json::parse(b, std::strlen(b), v));
}

TEST(json_writer_round_trip) {
  const std::string tricky = std::string("q\"b\\s/\x01\x1f\n") + "عربي 🙂";
  SecureString out;
  json::writeString(out, tricky);
  json::Value v;
  CHECK(json::parse(out.data(), out.size(), v) && str(v.s) == tricky);
  CHECK(str(out).find('\x01') == std::string::npos);
  SecureString n;
  json::writeInt(n, INT64_MIN);
  n += ' ';
  json::writeInt(n, 0);
  CHECK(str(n) == "-9223372036854775808 0");
}

TEST(entry_codec) {
  Entry e = test::sample("عنوان", "user", "https://x");
  e.id = 0xA1B2C3D4;
  e.favorite = true;
  e.created = -5;
  e.lastUsed = INT64_MAX;
  e.totp = "otpauth://totp/x?secret=AAAA";
  SecureBuf buf;
  CHECK(codec::encode(e, buf) && buf.size() == codec::encodedSize(e));
  Entry d;
  CHECK(codec::decode(buf.data(), buf.size(), d) && test::same(d, e));
  for (size_t cut = 0; cut < buf.size(); ++cut) CHECK(!codec::decode(buf.data(), cut, d));
  std::vector<uint8_t> extra(buf.data(), buf.data() + buf.size());
  extra.push_back(0);
  CHECK(!codec::decode(extra.data(), extra.size(), d));
  extra.pop_back();
  extra[0] = 2;  // unknown format
  CHECK(!codec::decode(extra.data(), extra.size(), d));
  extra[0] = 1;
  extra[5] = 2;  // unknown flag bit
  CHECK(!codec::decode(extra.data(), extra.size(), d));
}

TEST_MAIN()
