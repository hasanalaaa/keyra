#pragma once
// fido::Store over keyra::vault's passkey records (plain C++: also used by the
// host harness with the host vault).
#include "core/platform.hpp"

namespace keyra::fido {

class VaultStore final : public Store {
 public:
  bool unlocked() override;
  Result wrapKeys(WrapKeys& out) override;
  Result list(std::vector<Record>& out) override;
  Result put(uint32_t& id, const std::vector<uint8_t>& data) override;
  Result remove(uint32_t id) override;
  Result reset() override;
  bool pinSet() override;
  Result pinRead(std::vector<uint8_t>& out) override;
  Result pinWrite(const std::vector<uint8_t>& data) override;
};

}  // namespace keyra::fido
