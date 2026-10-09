// Shared by the upgrade test (built from this tree) and gen_old.cpp (built
// against the 3ce814a sources by regen.sh), so it may only use what both
// versions have: the vault's public API, keyra_fido's Authenticator over the
// real VaultStore, and keyra_api's activity encoding.
//
// A fixture directory is what a Keyra keeps across an update:
//   vault/...    every file of the "vault" LittleFS partition, byte for byte
//   nvs.txt      the two NVS values the vault and keyra_fido keep beside it
//                (unlock-failure counter, FIDO signature counter)
//   passkeys.txt one credential per line: rp, user byte, resident, ID, public key
//   recovery.txt the recovery key made on that version (hex)
//   backup.json  an encrypted backup with passkeys, exported by that version
#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "activity.hpp"
#include "core/cbor.hpp"
#include "core/ctap.hpp"
#include "fakes.hpp"
#include "host_crypto.hpp"
#include "keyra/fido.hpp"
#include "keyra/vault.hpp"
#include "keyra_test.hpp"
#include "vault_store.hpp"

namespace upgrade {

namespace vault = keyra::vault;
namespace fido = keyra::fido;
namespace activity = keyra::api::activity;
using Bytes = std::vector<uint8_t>;
namespace fs = std::filesystem;

// The storage behind vault::* (platform.cpp): what a reboot keeps. Reached
// through functions because keyra_vault and keyra_fido both have a
// "core/platform.hpp", so no file can include both components' internals.
std::map<std::string, Bytes>& flash();  // path in the vault partition -> bytes
uint32_t& vaultFailures();              // NVS: the vault's unlock-failure counter

inline const std::string kPass = "Keyra passphrase \xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a 2024";  // "... عربي 2024"
inline const std::string kBackupPass = "backup passphrase for fixtures";

// ---- hex and files ---------------------------------------------------------

inline std::string hex(const uint8_t* p, size_t n) {
  static const char* d = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < n; ++i) s += d[p[i] >> 4], s += d[p[i] & 15];
  return s;
}
inline std::string hex(const Bytes& b) { return hex(b.data(), b.size()); }
inline Bytes unhex(const std::string& s) {
  Bytes b;
  for (size_t i = 0; i + 1 < s.size(); i += 2) b.push_back(static_cast<uint8_t>(std::stoul(s.substr(i, 2), nullptr, 16)));
  return b;
}

inline std::string readText(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) {
    std::fprintf(stderr, "missing fixture file %s\n", p.c_str());
    std::exit(2);
  }
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
inline void writeText(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << s;
}

// Replaces the "flash" with a fixture's partition and NVS values; like a
// power cycle, nothing the vault held in RAM survives (call vault::init next).
inline void load(const fs::path& dir, uint32_t& fidoCounter) {
  vault::lock();
  flash().clear();
  const fs::path root = dir / "vault";
  for (const auto& e : fs::recursive_directory_iterator(root)) {
    if (!e.is_regular_file()) continue;
    const std::string s = readText(e.path());
    flash()[fs::relative(e.path(), root).generic_string()] = Bytes(s.begin(), s.end());
  }
  std::istringstream nvs(readText(dir / "nvs.txt"));
  std::string key;
  uint32_t value = 0;
  fidoCounter = 0;
  while (nvs >> key >> value) {
    if (key == "vault.failures") vaultFailures() = value;
    if (key == "fido.counter") fidoCounter = value;
  }
}

inline void dump(const fs::path& dir, uint32_t fidoCounter) {
  fs::remove_all(dir / "vault");
  for (const auto& [path, data] : flash())
    writeText(dir / "vault" / path, std::string(data.begin(), data.end()));
  writeText(dir / "nvs.txt", "vault.failures " + std::to_string(vaultFailures()) + "\nfido.counter " +
                                 std::to_string(fidoCounter) + "\n");
}

// ---- what 3ce814a wrote ------------------------------------------------------

// The accounts as they must read after every step on 3ce814a (gen_old.cpp
// makes them with put/touch; ids are random and checked separately).
inline std::string longText(size_t n, const char* seed) {
  std::string s;
  while (s.size() < n) s += seed;
  s.resize(n);
  return s;
}

inline std::vector<vault::Entry> expectedEntries() {
  std::vector<vault::Entry> v;
  vault::Entry e;

  // 2FA, notes with control characters, favorite, two old passwords, used.
  e = {};
  e.title = "Gmail";
  e.url = "https://accounts.google.com/";
  e.username = "hasan.alaa@example.com";
  e.password = "G00gle!Final";
  e.totp = "otpauth://totp/Google:hasan?secret=JBSWY3DPEHPK3PXP&issuer=Google&digits=6&period=30";
  e.notes = "line one\nline two\ttabbed \"quoted\" \\ back";
  e.favorite = true;
  e.created = 1700000000;
  e.updated = 1700200000;
  e.lastUsed = 1700300000;
  e.history = {{"second-pass", 1700200000}, {"first-pass", 1700100000}};
  v.push_back(e);

  // Arabic everywhere, emoji, an Arabic path in the URL.
  e = {};
  e.title = "\xd8\xa7\xd9\x84\xd8\xa8\xd9\x86\xd9\x83 \xd8\xa7\xd9\x84\xd8\xa3\xd9\x87\xd9\x84\xd9\x8a \xe2\x80\x94 "
            "\xd8\xad\xd8\xb3\xd8\xa7\xd8\xa8\xd9\x8a";  // البنك الأهلي — حسابي
  e.url = "https://bank.example.sa/\xd8\xaf\xd8\xae\xd9\x88\xd9\x84";  // /دخول
  e.username = "\xd8\xad\xd8\xb3\xd9\x86 \xd8\xb9\xd9\x84\xd8\xa7\xd8\xa1";  // حسن علاء
  e.password = "\xd9\x83\xd9\x84\xd9\x85\xd8\xa9-\xd8\xb3\xd8\xb1-\xd9\xa1\xd9\xa2\xd9\xa3-\xf0\x9f\x99\x82";  // كلمة-سر-١٢٣-🙂
  e.notes = "\xd8\xb1\xd9\x82\xd9\x85 \xd8\xa7\xd9\x84\xd8\xad\xd8\xb3\xd8\xa7\xd8\xa8: \xd9\xa0\xd9\xa1\xd9\xa2\xd9\xa3\n"
            "\xd9\x85\xd9\x84\xd8\xa7\xd8\xad\xd8\xb8\xd8\xa9 \xf0\x9f\x94\x90";  // رقم الحساب: ٠١٢٣ / ملاحظة 🔐
  e.created = 1710000000;
  e.updated = 1710000000;
  v.push_back(e);

  // Every field at its byte limit, and a full history (11 changes keep 10).
  e = {};
  e.title = longText(vault::kMaxTitle, "Long title ");
  e.url = "https://long.example.com/" + longText(vault::kMaxUrl - 25, "path/");
  e.username = longText(vault::kMaxUsername, "user.name+");
  e.password = longText(vault::kMaxPassword, "Pw!9#xZ_");
  e.totp = "JBSWY3DPEHPK3PXPJBSWY3DPEHPK3PXP";
  e.notes = longText(vault::kMaxNotes, "notes \xd9\x85 ");  // cut lands on ASCII: "notes م " is 9 bytes, 2048 = 9*227 + 5
  e.created = 1720000000;
  e.updated = 1720000000 + 11;
  for (int i = 10; i >= 1; --i) e.history.push_back({"long-old-" + std::to_string(i), 1720000000 + i + 1});
  v.push_back(e);

  // A custom auto-type sequence.
  e = {};
  e.title = "Bank with sequence";
  e.url = "https://seq.example.com";
  e.username = "user1";
  e.password = "p@ss {braces}";
  e.totp = "otpauth://totp/Seq?secret=GEZDGNBVGY3TQOJQ";
  e.sequence = "{USERNAME}{TAB}{DELAY 500}{PASSWORD}{ENTER}{PRESS}{TOTP}{ENTER}";
  e.created = 1730000000;
  e.updated = 1730000000;
  v.push_back(e);

  // Burn after typing: 3 allowed, one used.
  e = {};
  e.title = "One-time";
  e.password = "burn-me";
  e.burnAfter = 2;
  e.created = 1740000000;
  e.updated = 1740000000;
  e.lastUsed = 1740000100;
  v.push_back(e);

  // Nearly empty.
  e = {};
  e.title = "Wi-Fi";
  e.password = "wifi-pass";
  v.push_back(e);
  return v;
}

inline bool sameEntry(const vault::Entry& a, const vault::Entry& b) {
  if (a.history.size() != b.history.size()) return false;
  for (size_t i = 0; i < a.history.size(); ++i)
    if (a.history[i].password != b.history[i].password || a.history[i].changedAt != b.history[i].changedAt)
      return false;
  return a.title == b.title && a.url == b.url && a.username == b.username && a.password == b.password &&
         a.totp == b.totp && a.notes == b.notes && a.favorite == b.favorite && a.created == b.created &&
         a.updated == b.updated && a.lastUsed == b.lastUsed && a.sequence == b.sequence && a.burnAfter == b.burnAfter;
}

inline const vault::Entry* byTitle(const std::vector<vault::Entry>& v, const std::string& t) {
  for (const auto& e : v)
    if (e.title == t) return &e;
  return nullptr;
}

// Every expected entry is there exactly once and reads field for field the same.
inline bool entriesMatch(const std::vector<vault::Entry>& got, const std::vector<vault::Entry>& want) {
  bool ok = got.size() >= want.size();
  for (const auto& w : want) {
    const vault::Entry* g = byTitle(got, w.title);
    if (!g || !sameEntry(*g, w)) {
      std::fprintf(stderr, "entry differs or missing: %.40s\n", w.title.c_str());
      ok = false;
    }
  }
  return ok;
}

inline std::vector<activity::Event> expectedEvents() {
  std::vector<activity::Event> v;
  const char* titles[] = {"", "Gmail", "\xd8\xa7\xd9\x84\xd8\xa8\xd9\x86\xd9\x83", "MacBook \xd8\xad\xd8\xb3\xd9\x86"};
  for (uint8_t k = 1; k <= 18; ++k) {
    activity::Event e;
    e.kind = static_cast<activity::Kind>(k);
    e.at = k == 2 ? 0 : 1700000000 + int64_t(k) * 60;  // one with the clock unknown
    e.id = 0x1000u + k;
    e.n = k * 3u;
    e.detail = static_cast<uint8_t>(k % 5);
    e.title = titles[k % 4];
    v.push_back(e);
  }
  return v;
}

inline bool sameEvent(const activity::Event& a, const activity::Event& b) {
  return a.kind == b.kind && a.at == b.at && a.id == b.id && a.n == b.n && a.detail == b.detail && a.title == b.title;
}

inline bool readEvents(std::vector<activity::Event>& out) {
  Bytes raw;
  return vault::activityRead(raw) == vault::Status::Ok && activity::decode(raw, out);
}

// ---- passkeys ---------------------------------------------------------------

struct Cred {
  std::string rp;
  uint8_t user = 0;
  bool resident = false;
  Bytes id;
  uint8_t pub[65] = {};
};

inline Cred newCred(const std::string& rp, uint8_t user, bool resident) {
  Cred c;
  c.rp = rp;
  c.user = user;
  c.resident = resident;
  return c;
}

struct Fido {
  fido::test::OpenSslCrypto crypto;
  fido::VaultStore store;
  fido::test::MemCounter counter;
  fido::test::ScriptUser user;
  fido::test::MemAttestation attestation;
  fido::Authenticator auth{crypto, store, counter, attestation};

  uint8_t call(uint8_t cmd, const Bytes& params, keyra::fido::cbor::Value& out) {
    Bytes req{cmd};
    req.insert(req.end(), params.begin(), params.end());
    const Bytes r = auth.cbor(req.data(), req.size(), user, 0, 1);
    out = {};
    if (r.size() > 1 && !keyra::fido::cbor::decode(r.data() + 1, r.size() - 1, out)) return 0xFF;
    return r[0];
  }
};

inline std::string userName(uint8_t u) { return "user" + std::to_string(u) + "@example.com"; }
inline std::string displayName(uint8_t u) {
  return "\xd8\xad\xd8\xb3\xd9\x86 " + std::to_string(u);  // "حسن N"
}

// MakeCredential (ES256). extFlags: bit 0 hmac-secret, bits 1-2 credProtect level (0.3.0 only).
inline bool makeCred(Fido& f, Cred& c, int extFlags = 0) {
  using keyra::fido::cbor::Writer;
  Writer w;
  const bool ext = extFlags != 0;
  w.map(4 + (c.resident ? 1 : 0) + (ext ? 1 : 0));
  w.uint(1), w.bytes(Bytes(32, 0x11));
  w.uint(2), w.map(2), w.text("id"), w.text(c.rp), w.text("name"), w.text(c.rp);
  w.uint(3), w.map(3), w.text("id"), w.bytes(Bytes{c.user}), w.text("name"), w.text(userName(c.user)),
      w.text("displayName"), w.text(displayName(c.user));
  w.uint(4), w.array(1), w.map(2), w.text("alg"), w.integer(-7), w.text("type"), w.text("public-key");
  if (ext) {
    const int protect = (extFlags >> 1) & 3;
    w.uint(6), w.map((protect ? 1 : 0) + (extFlags & 1));
    if (protect) w.text("credProtect"), w.uint(static_cast<uint64_t>(protect));
    if (extFlags & 1) w.text("hmac-secret"), w.boolean(true);
  }
  if (c.resident) w.uint(7), w.map(1), w.text("rk"), w.boolean(true);
  keyra::fido::cbor::Value v;
  if (f.call(keyra::fido::ctap::kMakeCredential, w.out, v) != 0) return false;
  const auto* ad = v.find(2);
  if (!ad || ad->str.size() < 55 + 62) return false;
  const Bytes& a = ad->str;
  c.id.assign(a.begin() + 55, a.begin() + 55 + 62);
  keyra::fido::cbor::Value cose;
  // The COSE key is followed by the extensions map when there is one: decode just the key.
  size_t coseLen = a.size() - 55 - 62;
  while (coseLen > 0 && !keyra::fido::cbor::decode(a.data() + 55 + 62, coseLen, cose)) --coseLen;
  if (!cose.find(-2) || !cose.find(-3)) return false;
  c.pub[0] = 0x04;
  std::memcpy(c.pub + 1, cose.find(-2)->str.data(), 32);
  std::memcpy(c.pub + 33, cose.find(-3)->str.data(), 32);
  return true;
}

inline uint32_t counterOf(const Bytes& ad) {
  return uint32_t(ad[33]) << 24 | uint32_t(ad[34]) << 16 | uint32_t(ad[35]) << 8 | ad[36];
}

// GetAssertion naming c (or discoverable); true when the signature verifies
// with c's public key. *count: the signature counter; *name: user.name returned.
inline bool assertCred(Fido& f, const Cred& c, bool discoverable, uint32_t* count = nullptr,
                       std::string* name = nullptr) {
  using keyra::fido::cbor::Writer;
  const Bytes cdh(32, 0x22);
  Writer w;
  w.map(discoverable ? 2 : 3);
  w.uint(1), w.text(c.rp);
  w.uint(2), w.bytes(cdh);
  if (!discoverable)
    w.uint(3), w.array(1), w.map(2), w.text("id"), w.bytes(c.id), w.text("type"), w.text("public-key");
  keyra::fido::cbor::Value v;
  if (const uint8_t st = f.call(keyra::fido::ctap::kGetAssertion, w.out, v); st != 0) {
    std::fprintf(stderr, "GetAssertion for %s: CTAP status 0x%02x\n", c.rp.c_str(), st);
    return false;
  }
  const auto *cr = v.find(1), *ad = v.find(2), *sig = v.find(3);
  if (!cr || !cr->find("id") || cr->find("id")->str != c.id || !ad || !sig) return false;
  if (count) *count = counterOf(ad->str);
  if (name) {
    const auto* u = v.find(4);
    *name = u && u->find("name") ? std::string(u->find("name")->str.begin(), u->find("name")->str.end()) : "";
  }
  Bytes msg = ad->str;
  msg.insert(msg.end(), cdh.begin(), cdh.end());
  return fido::test::verifyEs256(c.pub, msg, sig->str);
}

inline std::string credLine(const Cred& c) {
  return c.rp + " " + std::to_string(c.user) + " " + (c.resident ? "1" : "0") + " " + hex(c.id) + " " +
         hex(c.pub, 65) + "\n";
}

inline std::vector<Cred> readCreds(const fs::path& p) {
  std::vector<Cred> out;
  std::istringstream in(readText(p));
  Cred c;
  int user = 0, rk = 0;
  std::string id, pub;
  while (in >> c.rp >> user >> rk >> id >> pub) {
    c.user = static_cast<uint8_t>(user);
    c.resident = rk != 0;
    c.id = unhex(id);
    const Bytes p65 = unhex(pub);
    std::memcpy(c.pub, p65.data(), 65);
    out.push_back(c);
  }
  return out;
}

}  // namespace upgrade
