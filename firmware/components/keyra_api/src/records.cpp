#include "records.hpp"

#include <vector>

#include "keyra/vault.hpp"

namespace keyra::api::records {
namespace {

Status from(vault::Status s) {
  switch (s) {
    case vault::Status::Ok: return Status::Ok;
    case vault::Status::Locked:
    case vault::Status::NotInitialized: return Status::Locked;
    case vault::Status::Corrupt: return Status::Corrupt;  // the sealed file itself does not open
    default: return Status::Storage;
  }
}

void wipe(std::vector<uint8_t>& v) {
  for (volatile uint8_t& b : v) b = 0;  // tag records hold AES keys
}

template <typename Store, typename Read>
Status loadWith(Read read, Store& out) {
  std::vector<uint8_t> raw;
  if (const Status st = from(read(raw)); st != Status::Ok) return st;
  auto parsed = Store::parse(raw.data(), raw.size());
  const bool known = Store::knownVersion(raw.data(), raw.size());
  wipe(raw);
  if (!parsed) return known ? Status::Corrupt : Status::Unsupported;
  out = std::move(*parsed);
  return Status::Ok;
}

template <typename Store, typename Read, typename Write>
Status saveWith(Read read, Write write, const Store& s) {
  Store current;
  if (const Status st = loadWith(read, current); st != Status::Ok) return st;
  std::vector<uint8_t> raw = s.serialize();
  const Status st = from(write(raw));
  wipe(raw);
  return st;
}

}  // namespace

Status load(tokens::Store& out) { return loadWith(vault::tokensRead, out); }
Status save(const tokens::Store& s) { return saveWith(vault::tokensRead, vault::tokensWrite, s); }
Status load(tags::Store& out) { return loadWith(vault::tagsRead, out); }
Status save(const tags::Store& s) { return saveWith(vault::tagsRead, vault::tagsWrite, s); }

const char* code(Status s) {
  switch (s) {
    case Status::Ok: return "ok";
    case Status::Locked: return "locked";
    case Status::Unsupported: return "unsupported_format";
    case Status::Corrupt: return "corrupt";
    case Status::Storage: break;
  }
  return "storage";
}

const char* message(Status s) {
  switch (s) {
    case Status::Locked: return "Vault is locked";
    case Status::Unsupported: return "Saved by a newer Keyra firmware; update Keyra to change it";
    case Status::Corrupt: return "Stored data is corrupt";
    default: break;
  }
  return "Could not read or save";
}

}  // namespace keyra::api::records
