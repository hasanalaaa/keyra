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
  e.sequence = "{USERNAME}{TAB}{PRESS}{PASSWORD}{ENTER}";
  e.burnAfter = 3;
  SecureBuf buf;
  CHECK(codec::encode(e, buf) && buf.size() == codec::encodedSize(e));
  Entry d;
  CHECK(codec::decode(buf.data(), buf.size(), d) && test::same(d, e));
  for (size_t cut = 0; cut < buf.size(); ++cut) CHECK(!codec::decode(buf.data(), cut, d));
  std::vector<uint8_t> extra(buf.data(), buf.data() + buf.size());
  extra.push_back(0);
  CHECK(!codec::decode(extra.data(), extra.size(), d));
  extra.pop_back();
  CHECK(extra[0] == 4);  // always written as the current format
  extra[0] = 5;          // unknown format
  CHECK(!codec::decode(extra.data(), extra.size(), d));
  extra[0] = 4;
  extra[5] = 2;  // unknown flag bit
  CHECK(!codec::decode(extra.data(), extra.size(), d));
  extra[5] = 1;
  extra.back() = uint8_t(kMaxBurnAfter + 1);  // burnAfter beyond the limit
  CHECK(!codec::decode(extra.data(), extra.size(), d));
  // Format 3 (no burnAfter byte) still reads, as "keep".
  std::vector<uint8_t> v3(buf.data(), buf.data() + buf.size() - 1);
  v3[0] = 3;
  CHECK(codec::decode(v3.data(), v3.size(), d) && d.burnAfter == 0 && d.sequence == e.sequence);
}

// Format 1 as written by v1.0/v1.1 firmware, built byte by byte.
static std::vector<uint8_t> v1Bytes(uint32_t id, const std::vector<std::string>& fields) {
  std::vector<uint8_t> b = {1};
  for (int i = 0; i < 4; ++i) b.push_back(uint8_t(id >> (8 * i)));
  b.push_back(1);  // favorite
  for (int64_t t : {int64_t(100), int64_t(200), int64_t(300)})
    for (int i = 0; i < 8; ++i) b.push_back(uint8_t(uint64_t(t) >> (8 * i)));
  for (const auto& f : fields) {
    b.push_back(uint8_t(f.size()));
    b.push_back(uint8_t(f.size() >> 8));
    b.insert(b.end(), f.begin(), f.end());
  }
  return b;
}

TEST(entry_codec_reads_v1) {
  auto b = v1Bytes(0x01020304, {"title", "url", "user", "pass", "", "notes"});
  Entry d;
  d.history.push_back({"stale", 1});  // decode replaces whatever was there
  CHECK(codec::decode(b.data(), b.size(), d));
  CHECK(d.id == 0x01020304 && d.favorite && d.created == 100 && d.updated == 200 &&
        d.lastUsed == 300 && d.title == "title" && d.password == "pass" && d.notes == "notes");
  CHECK(d.history.empty());
  b.push_back(0);  // v1 has no history block: trailing bytes are corruption
  CHECK(!codec::decode(b.data(), b.size(), d));
}

// Format 2 (v1.2 firmware): format 1 plus the history block, no sequence.
TEST(entry_codec_reads_v2) {
  auto b = v1Bytes(0x0A0B0C0D, {"title", "url", "user", "pass", "", "notes"});
  b[0] = 2;
  b.push_back(1);  // one old password
  for (int i = 0; i < 8; ++i) b.push_back(uint8_t(uint64_t(55) >> (8 * i)));
  b.push_back(3);
  b.push_back(0);
  b.insert(b.end(), {'o', 'l', 'd'});
  Entry d;
  d.sequence = "{PASSWORD}";  // decode replaces whatever was there
  CHECK(codec::decode(b.data(), b.size(), d));
  CHECK(d.history.size() == 1 && d.history[0].password == "old" && d.history[0].changedAt == 55);
  CHECK(d.sequence.empty());
  b.push_back(0);  // v2 has no sequence block: trailing bytes are corruption
  CHECK(!codec::decode(b.data(), b.size(), d));
}

// The sequence grammar guards every write, so a backup or import can never
// store something Keyra would refuse to type.
TEST(entry_codec_sequence) {
  Entry e = test::sample("s");
  e.id = 9;
  for (const char* ok : {"", "{USERNAME}{TAB}{PASSWORD}{ENTER}", "{PASSWORD}{DELAY 3000}{ENTER}",
                         "user@corp{TAB}{TOTP}", "{USERNAME}{ENTER}{PRESS}{PASSWORD}{ENTER}"}) {
    e.sequence = ok;
    CHECK(codec::valid(e));
    SecureBuf buf;
    Entry d;
    CHECK(codec::encode(e, buf) && codec::decode(buf.data(), buf.size(), d) && d.sequence == e.sequence);
  }
  for (const char* bad : {"{CTRL}v", "{ALT+F4}", "{WIN}", "{TAB", "}", "{DELAY 50}", "{PRESS}{PASSWORD}"}) {
    e.sequence = bad;
    CHECK(!codec::valid(e));
  }
  e.sequence = std::string(kMaxSequence + 1, 'a');
  CHECK(!codec::valid(e));
  // A stored invalid sequence is corruption, not something to type.
  e.sequence = "{TAB}";
  SecureBuf buf;
  CHECK(codec::encode(e, buf));
  std::vector<uint8_t> raw(buf.data(), buf.data() + buf.size());
  raw[raw.size() - 5] = 'W';  // {TAB} -> {WAB} (the last byte is burnAfter)
  Entry d;
  CHECK(!codec::decode(raw.data(), raw.size(), d));
}

TEST(entry_codec_history) {
  Entry e = test::sample("h");
  e.id = 7;
  for (int i = 0; i < int(kMaxHistory); ++i)
    e.history.push_back({i == 3 ? std::string("كلمة-🙂") : "old-" + std::to_string(i), 1700000000 + i});
  e.history[9].changedAt = 0;  // unknown date
  CHECK(codec::valid(e));
  SecureBuf buf;
  CHECK(codec::encode(e, buf) && buf.size() == codec::encodedSize(e));
  Entry d;
  CHECK(codec::decode(buf.data(), buf.size(), d) && test::same(d, e));
  for (size_t cut = 0; cut < buf.size(); ++cut) CHECK(!codec::decode(buf.data(), cut, d));

  // A count over the cap is corruption even if the bytes are all there.
  std::vector<uint8_t> raw(buf.data(), buf.data() + buf.size());
  size_t countAt = raw.size() - 1 - 2;  // before burnAfter and the (empty) sequence
  for (const auto& h : e.history) countAt -= 8 + 2 + h.password.size();
  CHECK(raw[countAt - 1] == kMaxHistory);
  raw[countAt - 1] = kMaxHistory + 1;
  CHECK(!codec::decode(raw.data(), raw.size(), d));

  Entry tooMany = e;
  tooMany.history.push_back({"one more", 1});
  CHECK(!codec::valid(tooMany));
  Entry tooLong = test::sample("l");
  tooLong.history.push_back({std::string(kMaxPassword + 1, 'a'), 1});
  CHECK(!codec::valid(tooLong));
  Entry badUtf8 = test::sample("u");
  badUtf8.history.push_back({"\xC3\x28", 1});
  CHECK(!codec::valid(badUtf8));
}

TEST_MAIN()
