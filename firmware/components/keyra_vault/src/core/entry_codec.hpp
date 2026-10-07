// Plaintext encoding of one Entry (what is encrypted into e/<id>.bin and what is
// kept in RAM while unlocked). Compact, length-prefixed, little-endian:
//
//   u8   format          = 2 (1 is still read)
//   u32  id
//   u8   flags           bit0 = favorite, other bits must be 0
//   i64  created, updated, lastUsed
//   6 × { u16 len, len bytes }   title, url, username, password, totp, notes
//   format 2 only:
//   u8   historyCount    ≤ kMaxHistory, newest first
//   historyCount × { i64 changedAt, u16 len, len bytes }   old passwords
//
// Format 1 is format 2 without the history block; such entries decode with an
// empty history and are rewritten as format 2 the next time they are stored.
// Strings are raw UTF-8 bytes (no terminator), so Arabic and any other script
// round-trip byte-exactly. Decoding rejects truncation, trailing bytes, unknown
// format/flags and fields over the limits.
#pragma once

#include "keyra/vault.hpp"
#include "secure_buf.hpp"

namespace keyra::vault::codec {

// Limits + UTF-8 validation of the user-supplied fields and the history.
bool valid(const Entry& e);
size_t encodedSize(const Entry& e);
// Always writes the current format. out is (re)allocated to encodedSize(e). False on OOM.
bool encode(const Entry& e, SecureBuf& out);
bool decode(const uint8_t* p, size_t n, Entry& out);

}  // namespace keyra::vault::codec
