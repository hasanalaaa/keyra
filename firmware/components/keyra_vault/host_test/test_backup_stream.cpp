#include <algorithm>
#include <array>
#include <string>

#include "check.hpp"
#include "core/backup_format.hpp"
#include "core/secure_buf.hpp"
#include "rig.hpp"

using namespace keyra::vault;
using namespace keyra::vault::test;

namespace {
constexpr char kBackupPass[] = "backup pass phrase";

std::string oldFile(Rig& rig, size_t count, size_t notes = 0, int version = 3) {
  backup::Envelope env;
  env.iterations = kTestIterations;
  for (size_t i = 0; i < sizeof env.salt; ++i) env.salt[i] = uint8_t(i + 1);
  for (size_t i = 0; i < sizeof env.iv; ++i) env.iv[i] = uint8_t(i + 31);
  SecureString plain;
  plain += version == 3 ? "{\"entries\":[" : "[";
  for (size_t i = 0; i < count; ++i) {
    Entry e = sample("account " + std::to_string(i));
    e.notes.assign(notes, 'n');
    if (i) plain += ',';
    backup::writeEntry(plain, e);  // the pre-streaming writer
    wipe(e);
  }
  plain += version == 3 ? "]}" : "]";
  uint8_t key[32] = {};
  CHECK(rig.crypto.pbkdf2Sha256(kBackupPass, env.salt, sizeof env.salt, env.iterations,
                                key, sizeof key));
  env.data.resize(plain.size() + 16);
  CHECK(rig.crypto.gcmSeal(key, env.iv, nullptr, 0,
                           reinterpret_cast<const uint8_t*>(plain.data()), plain.size(), env.data.data()));
  mem::zeroize(key, sizeof key);
  wipe(plain);
  std::string file = backup::writeEnvelope(env);  // the pre-streaming envelope writer
  if (version != 3) file.replace(file.find("\"v\":3"), 5, "\"v\":" + std::to_string(version));
  return file;
}

Status prepare(Rig& rig, const std::string& file, const std::string& pass, BackupImport& out,
               size_t chunkSize = 37) {
  size_t pos = 0;
  return rig->prepareBackupStream(pass, [&](char* dst, size_t cap, size_t& got) {
    got = std::min({chunkSize, cap, file.size() - pos});
    if (got) std::copy_n(file.data() + pos, got, dst);
    pos += got;
    return true;
  }, out);
}
}  // namespace

TEST(old_file_restores_with_streaming_reader_and_new_file_with_old_reader) {
  auto dst = Rig::ready();
  BackupImport prepared;
  const std::string old = oldFile(*dst, 3);
  CHECK(prepare(*dst, old, kBackupPass, prepared, 1) == Status::Ok);
  size_t added = 0, updated = 0;
  CHECK((*dst)->commitPreparedBackup(prepared, false, &added, &updated, nullptr) == Status::Ok);
  CHECK(added == 3 && updated == 0);
  std::string current;
  CHECK((*dst)->exportBackupStream(kBackupPass, [&current](const char* p, size_t n) {
    current.append(p, n);
    return true;
  }) == Status::Ok);
  auto second = Rig::ready();
  CHECK((*second)->importBackup(kBackupPass, current, true, &added, &updated) == Status::Ok);
  CHECK(added == 3);
}

TEST(streaming_writer_matches_old_v3_bytes_for_same_salt_and_iv) {
  auto src = Rig::ready();
  Entry a = sample("one"), b = sample("اثنان");
  CHECK((*src)->put(a) == Status::Ok && (*src)->put(b) == Status::Ok);
  std::string streamed;
  CHECK((*src)->exportBackupStream(kBackupPass, [&streamed](const char* p, size_t n) {
    streamed.append(p, n); return true;
  }) == Status::Ok);
  backup::Envelope env;
  CHECK(backup::readEnvelope(streamed, env));
  SecureString plain;
  plain += "{\"entries\":[";
  size_t count = 0;
  CHECK((*src)->forEach([&](const Entry& e) {
    if (count++) plain += ',';
    backup::writeEntry(plain, e);
    return true;
  }) == Status::Ok);
  plain += "]}";
  uint8_t key[32] = {};
  CHECK(src->crypto.pbkdf2Sha256(kBackupPass, env.salt, sizeof env.salt, env.iterations,
                                  key, sizeof key));
  env.data.resize(plain.size() + 16);
  CHECK(src->crypto.gcmSeal(key, env.iv, nullptr, 0,
                            reinterpret_cast<const uint8_t*>(plain.data()), plain.size(), env.data.data()));
  CHECK(streamed == backup::writeEnvelope(env));
  mem::zeroize(key, sizeof key);
  wipe(plain);
}

TEST(old_v1_v2_files_restore_with_streaming_reader) {
  for (int version : {1, 2}) {
    auto dst = Rig::ready();
    BackupImport prepared;
    CHECK(prepare(*dst, oldFile(*dst, 2, 0, version), kBackupPass, prepared, 13) == Status::Ok);
    size_t added = 0, updated = 0;
    CHECK((*dst)->commitPreparedBackup(prepared, false, &added, &updated, nullptr) == Status::Ok);
    CHECK(added == 2);
  }
}

TEST(streaming_passkeys_round_trip) {
  auto src = Rig::ready();
  uint32_t id = 0;
  CHECK((*src)->passkeyPut(id, std::vector<uint8_t>{1, 2, 3}) == Status::Ok);
  std::string file;
  CHECK((*src)->exportBackupStream(kBackupPass, [&file](const char* p, size_t n) {
    file.append(p, n); return true;
  }, true, 42) == Status::Ok);
  auto dst = Rig::ready();
  BackupImport prepared;
  CHECK(prepare(*dst, file, kBackupPass, prepared) == Status::Ok);
  size_t added = 0, updated = 0;
  PasskeyRestore pk;
  CHECK((*dst)->commitPreparedBackup(prepared, false, &added, &updated, &pk) == Status::Ok);
  CHECK(pk.present && pk.counter == 42 && pk.added == 1);
}

TEST(wrong_passphrase_precedes_plaintext_parse_errors) {
  auto dst = Rig::ready();
  for (size_t n : {size_t(0), size_t(1), size_t(17)}) {
    const std::string file = oldFile(*dst, n);
    BackupImport prepared;
    CHECK(prepare(*dst, file, "another wrong passphrase", prepared, 11) == Status::WrongPassphrase);
    std::vector<Entry> untouched;
    CHECK((*dst)->list(untouched) == Status::Ok && untouched.empty());
  }
}

TEST(modified_tag_never_changes_vault) {
  auto dst = Rig::ready();
  Entry existing = sample("keep");
  CHECK((*dst)->put(existing) == Status::Ok);
  backup::Envelope env;
  CHECK(backup::readEnvelope(oldFile(*dst, 2), env));
  env.data.back() ^= 0x40;
  BackupImport prepared;
  CHECK(prepare(*dst, backup::writeEnvelope(env), kBackupPass, prepared) == Status::WrongPassphrase);
  std::vector<Entry> after;
  CHECK((*dst)->list(after) == Status::Ok && after.size() == 1 && same(after[0], existing));
}

TEST(five_thousand_entries_authenticate_before_full) {
  auto dst = Rig::ready();
  Entry existing = sample("keep");
  CHECK((*dst)->put(existing) == Status::Ok);
  const std::string file = oldFile(*dst, 5000, 600);
  CHECK(file.size() > 4 * 1024 * 1024);
  BackupImport prepared;
  CHECK(prepare(*dst, file, "another wrong passphrase", prepared, 4096) == Status::WrongPassphrase);
  CHECK(prepare(*dst, file, kBackupPass, prepared, 4096) == Status::Full);
  std::vector<Entry> after;
  CHECK((*dst)->list(after) == Status::Ok && after.size() == 1 && same(after[0], existing));
}

TEST(truncated_file_never_changes_vault) {
  auto dst = Rig::ready();
  Entry existing = sample("keep");
  CHECK((*dst)->put(existing) == Status::Ok);
  std::string file = oldFile(*dst, 2);
  file.resize(file.size() - 10);
  BackupImport prepared;
  CHECK(prepare(*dst, file, kBackupPass, prepared) != Status::Ok);
  std::vector<Entry> after;
  CHECK((*dst)->list(after) == Status::Ok && after.size() == 1 && same(after[0], existing));
}

TEST_MAIN()
