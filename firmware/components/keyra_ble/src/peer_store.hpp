#pragma once
// What Keyra remembers about bonded hosts besides their keys: the name the
// host reports and when it last connected. NimBLE keeps the keys (NVS
// namespace "nimble_bond"); this lives in "keyra_ble". Host-task only.
#include <cstdint>
#include <string>

#include "keyra/ble.hpp"

namespace keyra::ble::peers {

struct Key {
  uint8_t type = 0;  // BLE_ADDR_PUBLIC / BLE_ADDR_RANDOM (identity)
  Addr addr{};
  bool operator==(const Key& o) const { return type == o.type && addr == o.addr; }
};

void load();
Peer get(const Key& k);                        // empty name / lastSeen 0 when unknown
void seen(const Key& k, int64_t unixSeconds);  // persisted
void named(const Key& k, const std::string& name);
void forget(const Key& k);
void clear();

}  // namespace keyra::ble::peers
