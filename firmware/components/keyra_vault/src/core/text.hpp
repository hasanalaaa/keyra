// Small text helpers shared by the codec, backup and TOTP parsers.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace keyra::vault::text {

// Strict UTF-8 (no overlongs, no surrogates, max U+10FFFF).
bool validUtf8(const std::string& s);
// Code points in an already-validated UTF-8 string.
size_t codePoints(const std::string& s);
void appendUtf8(std::string& out, uint32_t cp);

std::string base64Encode(const uint8_t* p, size_t n);
// Standard alphabet, padding required to a multiple of 4; no whitespace.
bool base64Decode(const std::string& in, std::vector<uint8_t>& out);

// RFC 4648 base32: case-insensitive, spaces ignored, '=' padding optional.
bool base32Decode(const std::string& in, std::vector<uint8_t>& out);

}  // namespace keyra::vault::text
