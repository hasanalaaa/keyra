// RFC 6238 TOTP on top of the Crypto seam (HMAC-SHA1/256/512).
#pragma once

#include <string>
#include <vector>

#include "platform.hpp"

namespace keyra::vault::totp {

struct Params {
  std::vector<uint8_t> secret;  // caller wipes (see generate)
  Hash hash = Hash::Sha1;
  int digits = 6;
  int period = 30;
};

// `otpauth://totp/<label>?secret=…[&algorithm=SHA1|SHA256|SHA512][&digits=6|8][&period=30|60]`
// or a bare base32 secret. HOTP and any other parameter values are rejected.
bool parse(const std::string& secretOrUri, Params& out);

// Parses, computes and wipes the decoded secret.
bool code(Crypto& crypto, const std::string& secretOrUri, int64_t unixTime, char out[11],
          int* period, int* remaining);

}  // namespace keyra::vault::totp
