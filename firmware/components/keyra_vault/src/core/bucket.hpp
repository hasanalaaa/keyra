// Bucket files: the entry layout of a vault in the 16 MB tail (docs/SPEC.md §6.1).
//
// Entries keep the record they had as e/<id>.bin (u8 1 | iv | ciphertext | tag,
// AAD "keyra/e/v1/<id>"), but kCount of them share one file, so an unlock opens
// kCount files instead of one per entry (littlefs walks a directory for every
// open: docs/research/CAPACITY.md).
//
//   b/<kk>.bin  kk = id % kCount as 2 lowercase hex digits
//               u8 version=1 | u32 count | count × (u32 id | u32 len | record[len])
//               (LE; ids non-zero, strictly ascending, each ≡ kk mod kCount)
//   b/<kk>.new  a replace-restore's staged bucket, same format
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace keyra::vault::bucket {

inline constexpr uint32_t kCount = 128;
inline constexpr uint8_t kVersion = 1;
inline constexpr char kDir[] = "b";

inline uint32_t of(uint32_t id) { return id % kCount; }
std::string path(uint32_t k);        // "b/1f.bin"
std::string stagedPath(uint32_t k);  // "b/1f.new"
// "1f.bin" (ext ".bin") or "1f.new" → k; false for any other name.
bool parseName(const std::string& name, uint32_t& k, const char* ext);

struct Record {
  uint32_t id = 0;
  std::vector<uint8_t> sealed;  // exactly what e/<id>.bin holds
};
using Records = std::vector<Record>;

// False unless `file` is a well-formed bucket k (version, framing, id rules).
bool parse(const std::vector<uint8_t>& file, uint32_t k, Records& out);
// Sorts by id. Ids must be distinct and belong to one bucket.
std::vector<uint8_t> encode(Records& records);

}  // namespace keyra::vault::bucket
