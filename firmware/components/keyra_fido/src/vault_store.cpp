#include "vault_store.hpp"

#include <algorithm>

#include "core/cred.hpp"
#include "keyra/fido.hpp"
#include "keyra/vault.hpp"

namespace keyra::fido {
namespace {

Store::Result from(vault::Status s) {
  switch (s) {
    case vault::Status::Ok: return Store::Result::Ok;
    case vault::Status::Locked:
    case vault::Status::NotInitialized: return Store::Result::Locked;
    case vault::Status::Full: return Store::Result::Full;
    case vault::Status::NotFound: return Store::Result::NotFound;
    default: return Store::Result::Error;
  }
}

}  // namespace

static_assert(cred::kMaxResident == vault::kMaxPasskeys && kMaxPasskeys == vault::kMaxPasskeys, "one limit");

bool VaultStore::unlocked() { return vault::unlocked(); }

static_assert(WrapKeys::kMax == vault::kMaxPasskeyWrapKeys, "one limit");

Store::Result VaultStore::wrapKeys(WrapKeys& out) {
  out.clear();
  const auto s = from(vault::passkeyWrapKeys(out.key, out.count));
  if (s == Result::Ok && out.count == 0) return Result::Error;
  return s;
}

Store::Result VaultStore::list(std::vector<Record>& out) {
  out.clear();
  std::vector<vault::PasskeyRecord> recs;
  const auto s = from(vault::passkeyList(recs));
  for (auto& r : recs) out.push_back(Record{r.id, std::move(r.data)});
  return s;
}

Store::Result VaultStore::put(uint32_t& id, const std::vector<uint8_t>& data) {
  return from(vault::passkeyPut(id, data));
}

Store::Result VaultStore::remove(uint32_t id) { return from(vault::passkeyRemove(id)); }

Store::Result VaultStore::reset() { return from(vault::passkeyReset()); }

// ---- keyra/fido.hpp: Settings → Passkeys -----------------------------------

Result list(std::vector<Passkey>& out) {
  out.clear();
  std::vector<vault::PasskeyRecord> recs;
  switch (vault::passkeyList(recs)) {
    case vault::Status::Ok: break;
    case vault::Status::Locked:
    case vault::Status::NotInitialized: return Result::Locked;
    default: return Result::Error;
  }
  for (const auto& r : recs) {
    cred::Resident x;
    Passkey p;
    p.id = r.id;
    if (cred::decode(r.data, x)) {
      p.rpId = x.rpId;
      p.userName = x.userName;
      p.displayName = x.displayName;
      p.created = x.created;
    }  // an undecodable record is still listed so it can be deleted
    out.push_back(std::move(p));
  }
  std::stable_sort(out.begin(), out.end(), [](const Passkey& a, const Passkey& b) { return a.created > b.created; });
  return Result::Ok;
}

Result remove(uint32_t id) {
  switch (vault::passkeyRemove(id)) {
    case vault::Status::Ok: return Result::Ok;
    case vault::Status::Locked:
    case vault::Status::NotInitialized: return Result::Locked;
    case vault::Status::NotFound: return Result::NotFound;
    default: return Result::Error;
  }
}

}  // namespace keyra::fido
