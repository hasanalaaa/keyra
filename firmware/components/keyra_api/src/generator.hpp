#pragma once
// Password generator core (SPEC §9.1). Plain C++ with the random source
// injected, so the device feeds it the hardware RNG and host tests a seeded one.
//
// Every character is drawn uniformly from the union of the enabled classes by
// rejection sampling (no modulo bias). Class minimums are met by rejecting the
// whole candidate and drawing a new one, never by placing required characters:
// the result is uniform over all passwords that satisfy the settings, so no
// position or class is favoured. Each enabled class appears at least once;
// minDigits/minSymbols can raise that floor.
#include <cstdint>
#include <functional>
#include <string>

namespace keyra::gen {

constexpr int kMinLength = 8, kMaxLength = 128;
constexpr const char* kDefaultSymbols = "!@#$%^&*-_=+?";
// Characters that read alike in many fonts (SPEC §9.1).
constexpr const char* kAmbiguous = "0Oo1lI|`'\"";
constexpr size_t kMaxSymbolSet = 32;
// Settings whose candidates meet the minimums less often than this are refused:
// rejection sampling would need too many draws (expected tries = 1/acceptance).
constexpr double kMinAcceptance = 1e-3;
// Never reached when acceptance ≥ kMinAcceptance: (1 - 1e-3)^100000 ≈ e^-100.
constexpr int kMaxAttempts = 100000;

struct Params {
  int length = 20;
  bool lower = true, upper = true, digits = true, symbols = true;
  int minDigits = 0, minSymbols = 0;  // 0 and 1 mean the same: at least one
  bool avoidAmbiguous = false;
  std::string symbolSet;  // empty = kDefaultSymbols
};

enum class Error { None, Length, NoClass, Minimum, SymbolSet, EmptyClass, TooStrict };

struct Plan {
  struct Class {
    std::string chars;
    int min = 0;
  };
  Class classes[4];
  int classCount = 0;
  int length = 0;
  std::string alphabet;     // all classes concatenated; classes are disjoint
  int8_t classOf[128] = {};  // ASCII → index into classes, -1 if not in the alphabet
  double acceptance = 0;    // P(a uniform candidate meets every minimum)
};

Error plan(const Params& p, Plan& out);
// log2 of how many distinct passwords the plan can produce (all equally likely).
double entropyBits(const Plan& plan);
// Exact probability that `length` uniform draws from the classes meet every
// minimum (exponential generating functions; terms are all positive, so double
// precision is plenty for a threshold and for entropy).
double acceptance(const Plan& plan);

// Fills `n` bytes; false when the source failed.
using Random = std::function<bool(uint8_t* out, size_t n)>;
// Writes the password into `out` (its buffer is reused for every candidate and
// wiped on failure). False when the RNG failed or kMaxAttempts ran out.
bool generate(const Plan& plan, const Random& random, std::string& out);

const char* message(Error e);  // English, for the 400 response

}  // namespace keyra::gen
