#pragma once

#include <functional>
#include <vector>

#include "backup_format.hpp"
#include "platform.hpp"
#include "secure_buf.hpp"

namespace keyra::vault::backup {

// Pulls the v1-v3 envelope in bounded pieces. The source returns got=0 at EOF;
// false means transport failure. No caller may commit entries until this returns Ok.
using Source = std::function<bool(char*, size_t, size_t& got)>;
using Entries = std::vector<Entry, ZeroingAllocator<Entry>>;
Status readStream(Crypto& crypto, const std::string& passphrase, const Source& source,
                  Entries& entries, Passkeys& passkeys);

}  // namespace keyra::vault::backup
