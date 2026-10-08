#pragma once
// Helpers shared by ctap.cpp and ctap_pin.cpp (internal to the core).
#include "cbor.hpp"
#include "ctap.hpp"

namespace keyra::fido::ctap {

inline uint8_t fromAnswer(User::Answer a) {
  switch (a) {
    case User::Answer::Approved: return kOk;
    case User::Answer::Denied: return kOperationDenied;
    case User::Answer::Timeout: return kUserActionTimeout;
    case User::Answer::Cancelled: return kKeepaliveCancel;
  }
  return kOther;
}

// Unlock wait has its own meaning on timeout: Keyra stayed locked.
inline uint8_t fromUnlock(User::Answer a) {
  return a == User::Answer::Timeout ? uint8_t{kOperationDenied} : fromAnswer(a);
}

// Parses the request map. Keys must be unsigned integers (CTAP2 parameters).
inline uint8_t parseParams(const uint8_t* p, size_t n, cbor::Value& out) {
  using T = cbor::Value::Type;
  if (n == 0) return kMissingParameter;  // every command below needs parameters
  if (!cbor::decode(p, n, out)) return kInvalidCbor;
  if (out.type != T::Map) return kCborUnexpectedType;
  for (const auto& e : out.entries)
    if (e.first.type != T::Uint) return kCborUnexpectedType;
  return kOk;
}

}  // namespace keyra::fido::ctap
