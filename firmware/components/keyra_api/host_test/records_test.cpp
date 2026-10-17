// tokens.bin / tags.bin through the vault (records.cpp): a record this firmware
// cannot read is an error, and creating a token or a tag never writes over it.
#include <string>
#include <vector>

#include "keyra/vault.hpp"
#include "keyra_test.hpp"
#include "records.hpp"

using namespace keyra::api;
namespace vault = keyra::vault;
using Bytes = std::vector<uint8_t>;

namespace {

tokens::Token token(uint32_t id) {
  tokens::Token t;
  t.id = id;
  t.hash[0] = 1;
  t.name = "Claude";
  return t;
}

tags::Tag tag(uint32_t id) {
  tags::Tag t;
  t.id = id;
  t.name = "Monitor";
  t.entry = 9;
  t.what = tags::What::Password;
  t.hash[0] = 1;
  return t;
}

Bytes storedTokens() {
  Bytes b;
  CHECK(vault::tokensRead(b) == vault::Status::Ok);
  return b;
}
Bytes storedTags() {
  Bytes b;
  CHECK(vault::tagsRead(b) == vault::Status::Ok);
  return b;
}

// A record from a newer firmware (version 2) and a damaged one (version 1,
// count past the limit): load refuses, and saving a new store leaves it byte for byte.
void unreadableTokensAreKept() {
  const Bytes future = {2, 1, 0xDE, 0xAD, 0xBE, 0xEF, 'n', 'e', 'w'};
  const Bytes damaged = {1, 200, 0};
  for (const Bytes& stored : {future, damaged}) {
    const records::Status want = stored[0] == 2 ? records::Status::Unsupported : records::Status::Corrupt;
    CHECK(vault::tokensWrite(stored) == vault::Status::Ok);
    tokens::Store s;
    CHECK(records::load(s) == want);
    CHECK(s.all().empty());
    tokens::Store created;  // what POST /api/tokens would save
    CHECK(created.add(token(7)));
    CHECK(records::save(created) == want);
    CHECK(storedTokens() == stored);
  }
  CHECK(std::string(records::code(records::Status::Unsupported)) == "unsupported_format");
  CHECK(std::string(records::code(records::Status::Corrupt)) == "corrupt");
}

void unreadableTagsAreKept() {
  const Bytes future = {7, 0, 1, 2, 3};
  const Bytes damaged = {1, 99};
  for (const Bytes& stored : {future, damaged}) {
    const records::Status want = stored[0] == 7 ? records::Status::Unsupported : records::Status::Corrupt;
    CHECK(vault::tagsWrite(stored) == vault::Status::Ok);
    tags::Store s;
    CHECK(records::load(s) == want);
    tags::Store created;
    CHECK(created.add(tag(5)));
    CHECK(records::save(created) == want);
    CHECK(storedTags() == stored);
  }
}

// No record = empty; a readable one is read and replaced as before.
void readableRecordsWork() {
  CHECK(vault::tokensWrite({}) == vault::Status::Ok && vault::tagsWrite({}) == vault::Status::Ok);
  tokens::Store tk;
  CHECK(records::load(tk) == records::Status::Ok && tk.all().empty());
  CHECK(tk.add(token(3)));
  CHECK(records::save(tk) == records::Status::Ok);
  tokens::Store tk2;
  CHECK(records::load(tk2) == records::Status::Ok && tk2.all().size() == 1 && tk2.all()[0].id == 3);
  CHECK(tk2.add(token(4)) && records::save(tk2) == records::Status::Ok);

  tags::Store tg;
  CHECK(records::load(tg) == records::Status::Ok && tg.all().empty());
  CHECK(tg.add(tag(8)) && records::save(tg) == records::Status::Ok);
  tags::Store tg2;
  CHECK(records::load(tg2) == records::Status::Ok && tg2.all().size() == 1);

  vault::lock();
  CHECK(records::load(tk) == records::Status::Locked);
  CHECK(records::save(tk) == records::Status::Locked);
}

// F10: once this unlock has read the record, a token or tag it does not hold
// is refused before any read. A damaged record underneath proves the order: a
// read would answer Corrupt, the index answers Absent without one.
void unknownKeysAreRefusedBeforeAnyRead() {
  CHECK(vault::tokensWrite({}) == vault::Status::Ok && vault::tagsWrite({}) == vault::Status::Ok);
  tokens::Store tk;
  CHECK(tk.add(token(3)) && records::save(tk) == records::Status::Ok);
  tags::Store tg;
  CHECK(tg.add(tag(8)) && records::save(tg) == records::Status::Ok);
  const tokens::Digest known = token(3).hash;
  tokens::Digest stranger = known;
  stranger[31] ^= 1;

  records::KnownTokens kt;
  records::KnownTags kg;
  tokens::Store out;
  tags::Store outTags;
  // Nothing read yet in this unlock: the index is not trusted, the record is read.
  CHECK(records::loadFor(stranger, out, kt) == records::Status::Ok && !out.find(stranger));
  CHECK(records::loadFor(99, tags::Kind::Simple, outTags, kg) == records::Status::Ok && !outTags.find(99));

  const Bytes damaged = {1, 200, 0};
  CHECK(vault::tokensWrite(damaged) == vault::Status::Ok && vault::tagsWrite({1, 99}) == vault::Status::Ok);
  tokens::Store none;
  CHECK(records::loadFor(stranger, none, kt) == records::Status::Absent && none.all().empty());
  CHECK(records::loadFor(99, tags::Kind::Simple, outTags, kg) == records::Status::Absent);
  CHECK(records::loadFor(8, tags::Kind::Secure, outTags, kg) == records::Status::Absent);  // wrong kind
  // What is stored is always read, and a damaged record still fails closed.
  CHECK(records::loadFor(known, none, kt) == records::Status::Corrupt);
  CHECK(records::loadFor(8, tags::Kind::Simple, outTags, kg) == records::Status::Corrupt);

  // Another unlock: the index is stale and the record is read again.
  vault::lock();
  CHECK(vault::unlock("records test passphrase", nullptr) == vault::Status::Ok);
  CHECK(records::loadFor(stranger, none, kt) == records::Status::Corrupt);
  CHECK(records::loadFor(99, tags::Kind::Simple, outTags, kg) == records::Status::Corrupt);

  // A save keeps the index current (a token made or revoked in this unlock).
  CHECK(vault::tokensWrite({}) == vault::Status::Ok);
  tokens::Store made;
  CHECK(made.add(token(5)) && records::save(made) == records::Status::Ok);
  records::remember(made, kt);
  CHECK(records::loadFor(token(5).hash, none, kt) == records::Status::Ok && none.find(token(5).hash));
}

}  // namespace

int main() {
  CHECK(vault::init() == vault::Status::Ok && vault::setup("records test passphrase") == vault::Status::Ok);
  unreadableTokensAreKept();
  unreadableTagsAreKept();
  unknownKeysAreRefusedBeforeAnyRead();
  readableRecordsWork();
  return KEYRA_TEST_RESULT();
}
