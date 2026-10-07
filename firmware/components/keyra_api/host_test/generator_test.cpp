// Password generator core (SPEC §9.1): validation, exact acceptance/entropy,
// edge lengths, minimums at the limit, and chi-square checks (seeded RNG) that
// there is no character, class or positional bias — plus proof that the
// statistics would catch the classic biased shortcuts.
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "generator.hpp"
#include "keyra_test.hpp"

using namespace keyra::gen;

namespace {

// SplitMix64: tiny, well-distributed, and seedable so every run is identical.
struct TestRng {
  uint64_t s;
  explicit TestRng(uint64_t seed) : s(seed) {}
  uint64_t next() {
    uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  Random source() {
    return [this](uint8_t* out, size_t n) {
      for (size_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>(next() >> 56);
      return true;
    };
  }
};

Params params(int length, bool lower, bool upper, bool digits, bool symbols) {
  Params p;
  p.length = length;
  p.lower = lower, p.upper = upper, p.digits = digits, p.symbols = symbols;
  return p;
}

int countIn(const std::string& s, const std::string& set) {
  int n = 0;
  for (char c : s) n += set.find(c) != std::string::npos;
  return n;
}

// Upper critical value of chi-square with `dof` degrees of freedom at
// α = 1e-4 (Wilson–Hilferty; z = 3.719). Conservative so a seeded run never
// flakes, yet far below what any real bias produces at these sample sizes.
double chiCritical(int dof) {
  const double d = dof, z = 3.719;
  const double t = 1 - 2 / (9 * d) + z * std::sqrt(2 / (9 * d));
  return d * t * t * t;
}

double chiSquare(const std::vector<double>& observed, const std::vector<double>& expected) {
  double x = 0;
  for (size_t i = 0; i < observed.size(); ++i) x += (observed[i] - expected[i]) * (observed[i] - expected[i]) / expected[i];
  return x;
}

// Every character uniform within its class (and across the alphabet when one class).
bool uniformWithinClasses(const Plan& plan, const std::vector<std::string>& pws) {
  std::map<char, double> seen;
  for (const auto& pw : pws)
    for (char c : pw) seen[c] += 1;
  bool ok = true;
  for (int k = 0; k < plan.classCount; ++k) {
    const std::string& chars = plan.classes[k].chars;
    std::vector<double> obs, exp;
    double total = 0;
    for (char c : chars) total += seen[c];
    for (char c : chars) {
      obs.push_back(seen[c]);
      exp.push_back(total / chars.size());
    }
    const double x = chiSquare(obs, exp);
    if (x > chiCritical(static_cast<int>(chars.size()) - 1)) {
      std::fprintf(stderr, "class %d: chi2 %.1f > %.1f\n", k, x, chiCritical(static_cast<int>(chars.size()) - 1));
      ok = false;
    }
  }
  return ok;
}

// Every class equally likely at every position (exchangeable positions).
bool noPositionalBias(const Plan& plan, const std::vector<std::string>& pws) {
  bool ok = true;
  for (int k = 0; k < plan.classCount; ++k) {
    std::vector<double> obs(plan.length, 0);
    for (const auto& pw : pws)
      for (int i = 0; i < plan.length; ++i) obs[i] += plan.classOf[static_cast<unsigned char>(pw[i])] == k;
    double total = 0;
    for (double o : obs) total += o;
    const std::vector<double> exp(plan.length, total / plan.length);
    const double x = chiSquare(obs, exp);
    if (x > chiCritical(plan.length - 1)) {
      std::fprintf(stderr, "class %d by position: chi2 %.1f > %.1f\n", k, x, chiCritical(plan.length - 1));
      ok = false;
    }
  }
  return ok;
}

void validation() {
  Plan plan;
  CHECK(keyra::gen::plan(params(7, true, true, true, true), plan) == Error::Length);
  CHECK(keyra::gen::plan(params(129, true, true, true, true), plan) == Error::Length);
  CHECK(keyra::gen::plan(params(8, true, true, true, true), plan) == Error::None);
  CHECK(keyra::gen::plan(params(128, true, true, true, true), plan) == Error::None);
  CHECK(keyra::gen::plan(params(20, false, false, false, false), plan) == Error::NoClass);

  Params p = params(20, true, true, false, true);
  p.minDigits = 2;  // digits are off
  CHECK(keyra::gen::plan(p, plan) == Error::Minimum);
  p = params(20, true, true, true, false);
  p.minSymbols = 1;
  CHECK(keyra::gen::plan(p, plan) == Error::Minimum);
  p = params(20, true, true, true, true);
  p.minDigits = -1;
  CHECK(keyra::gen::plan(p, plan) == Error::Minimum);
  p.minDigits = 21;
  CHECK(keyra::gen::plan(p, plan) == Error::Minimum);
  p = params(20, false, false, true, false);
  p.minDigits = 0;  // 0 = "at least one", same as 1
  CHECK(keyra::gen::plan(p, plan) == Error::None && plan.classes[0].min == 1);

  for (const char* bad : {"ab", "!a", "! ", "!!", "\x7f", "é", "!@#$%^&*()_+-=[]{};:,.<>/?|~`'\"\\X"}) {
    p = params(20, true, true, true, true);
    p.symbolSet = bad;
    CHECK(keyra::gen::plan(p, plan) == Error::SymbolSet);
  }
  p.symbolSet = "!@#$%^&*()_+-=[]{};:,.<>/?|~`'\"\\";  // all 32 ASCII punctuation
  CHECK(keyra::gen::plan(p, plan) == Error::None && plan.classes[3].chars.size() == 32);
  p.avoidAmbiguous = true;
  CHECK(keyra::gen::plan(p, plan) == Error::None && plan.classes[3].chars.size() == 28);  // | ` ' " gone
  CHECK(plan.classes[0].chars.find_first_of("ol") == std::string::npos);
  CHECK(plan.classes[1].chars.find_first_of("OI") == std::string::npos);
  CHECK(plan.classes[2].chars == "23456789");
  p.symbolSet = "|`";
  CHECK(keyra::gen::plan(p, plan) == Error::EmptyClass);

  // Layout-proof (SPEC §10.2): only the allowed characters survive, per class.
  p = params(24, true, true, true, true);
  p.restrict = true;
  p.allowed = "abcxyzABC0123!#";
  CHECK(keyra::gen::plan(p, plan) == Error::None);
  CHECK(plan.classes[0].chars == "abcxyz" && plan.classes[1].chars == "ABC" && plan.classes[2].chars == "0123");
  CHECK(plan.classes[3].chars == "!#");
  p.allowed = "0123456789!@";  // e.g. US + Arabic: no letters in common
  CHECK(keyra::gen::plan(p, plan) == Error::EmptyClass);
  p.lower = p.upper = false;
  CHECK(keyra::gen::plan(p, plan) == Error::None && plan.alphabet == "0123456789!@");

  p = params(128, true, true, true, true);
  p.minDigits = 60;  // ~17 digits expected in 128 draws: hopeless
  CHECK(keyra::gen::plan(p, plan) == Error::TooStrict);
  CHECK(std::string(message(Error::TooStrict)).find("too high") != std::string::npos);
}

// Exact acceptance against brute-force enumeration of every candidate. Real
// plans are ≥ 8 long, too many to enumerate, so the plan is shortened after
// validation (acceptance() only reads the classes and the length).
void acceptanceMatchesEnumeration() {
  Params a = params(8, false, false, true, true);
  a.symbolSet = "!@#";
  a.minDigits = 2, a.minSymbols = 2;
  Params b = params(8, false, true, true, true);
  b.symbolSet = "!@";
  b.avoidAmbiguous = true;  // 24 upper, 8 digits
  b.minDigits = 2;
  const std::pair<Params, int> cases[] = {{a, 5}, {b, 4}};
  for (const auto& c : cases) {
    Plan plan;
    CHECK(keyra::gen::plan(c.first, plan) == Error::None);
    plan.length = c.second;
    plan.acceptance = acceptance(plan);
    const size_t n = plan.alphabet.size();
    size_t total = 1, good = 0;
    for (int i = 0; i < plan.length; ++i) total *= n;
    for (size_t s = 0; s < total; ++s) {
      int counts[4] = {};
      size_t v = s;
      for (int i = 0; i < plan.length; ++i, v /= n) ++counts[plan.classOf[static_cast<unsigned char>(plan.alphabet[v % n])]];
      bool ok = true;
      for (int k = 0; k < plan.classCount; ++k) ok &= counts[k] >= plan.classes[k].min;
      good += ok;
    }
    const double exact = static_cast<double>(good) / static_cast<double>(total);
    CHECK(std::fabs(plan.acceptance - exact) < 1e-12);
    CHECK(std::fabs(entropyBits(plan) - std::log2(static_cast<double>(good))) < 1e-9);
  }
}

void entropy() {
  Plan plan;
  CHECK(keyra::gen::plan(params(10, false, false, true, false), plan) == Error::None);
  CHECK(std::fabs(entropyBits(plan) - 10 * std::log2(10.0)) < 1e-9);  // nothing rejected
  CHECK(keyra::gen::plan(params(20, true, true, true, true), plan) == Error::None);
  const double all = 20 * std::log2(75.0);
  CHECK(entropyBits(plan) < all && entropyBits(plan) > all - 1);  // minimums cost < 1 bit here
  CHECK(plan.alphabet.size() == 26 + 26 + 10 + 13);
}

void edgeLengthsAndMinimumsAtTheLimit() {
  TestRng rng(1);
  const Random src = rng.source();
  for (int length : {8, 128}) {
    Plan plan;
    CHECK(keyra::gen::plan(params(length, true, true, true, true), plan) == Error::None);
    for (int i = 0; i < 200; ++i) {
      std::string pw;
      CHECK(generate(plan, src, pw));
      CHECK(static_cast<int>(pw.size()) == length);
      for (int k = 0; k < plan.classCount; ++k) CHECK(countIn(pw, plan.classes[k].chars) >= 1);
      CHECK(countIn(pw, plan.alphabet) == length);
    }
  }
  // Floors summing exactly to the length: 1 + 1 + 3 + 3 = 8, acceptance ≈ 1.7e-3.
  Params p = params(8, true, true, true, true);
  p.minDigits = 3, p.minSymbols = 3;
  Plan plan;
  CHECK(keyra::gen::plan(p, plan) == Error::None && plan.acceptance > kMinAcceptance && plan.acceptance < 2e-3);
  for (int i = 0; i < 50; ++i) {
    std::string pw;
    CHECK(generate(plan, src, pw));
    CHECK(countIn(pw, plan.classes[0].chars) == 1 && countIn(pw, plan.classes[1].chars) == 1);
    CHECK(countIn(pw, plan.classes[2].chars) == 3 && countIn(pw, plan.classes[3].chars) == 3);
  }
  p.minSymbols = 4;  // 9 > 8
  CHECK(keyra::gen::plan(p, plan) == Error::Minimum);
  p = params(8, false, false, true, true);
  p.minDigits = 4, p.minSymbols = 4;
  CHECK(keyra::gen::plan(p, plan) == Error::None);
  std::string pw;
  CHECK(generate(plan, src, pw) && countIn(pw, "0123456789") == 4);
}

void rejectionAndRngFailure() {
  Plan plan;
  CHECK(keyra::gen::plan(params(8, false, false, true, false), plan) == Error::None);  // n = 10, limit 250
  std::vector<uint8_t> script = {255, 250, 3, 4, 5, 6, 7, 8, 9, 10};
  size_t at = 0;
  const Random scripted = [&](uint8_t* out, size_t n) {
    for (size_t i = 0; i < n; ++i) out[i] = at < script.size() ? script[at++] : 0;
    return true;
  };
  std::string pw;
  CHECK(generate(plan, scripted, pw));
  CHECK(pw == "34567890");  // 255 and 250 redrawn, 10 % 10 = 0

  const Random broken = [](uint8_t*, size_t) { return false; };
  pw = "stale";
  CHECK(!generate(plan, broken, pw) && pw.empty());
}

void statistics() {
  TestRng rng(20261007);
  const Random src = rng.source();
  Plan plan;
  CHECK(keyra::gen::plan(params(20, true, true, true, true), plan) == Error::None);  // 75 characters
  std::vector<std::string> pws(20000);
  for (auto& pw : pws) CHECK(generate(plan, src, pw));
  CHECK(uniformWithinClasses(plan, pws));
  CHECK(noPositionalBias(plan, pws));

  // With minimums at work, the same must hold (rejection keeps exchangeability).
  Params p = params(12, true, true, true, true);
  p.minDigits = 3, p.minSymbols = 3;
  Plan strict;
  CHECK(keyra::gen::plan(p, strict) == Error::None);
  std::vector<std::string> strictPws(20000);
  for (auto& pw : strictPws) CHECK(generate(strict, src, pw));
  CHECK(uniformWithinClasses(strict, strictPws));
  CHECK(noPositionalBias(strict, strictPws));

  // The tests have teeth: plain modulo (no rejection) favours the first
  // 256 % 75 = 31 characters, and "required characters first" fixes classes to
  // positions. Both must be flagged.
  std::vector<std::string> modulo(20000), placed(20000);
  for (auto& pw : modulo) {
    pw.resize(20);
    for (char& c : pw) c = plan.alphabet[(rng.next() >> 56) % plan.alphabet.size()];
  }
  std::fprintf(stderr, "(expected) ");
  CHECK(!uniformWithinClasses(plan, modulo));
  for (auto& pw : placed) {
    CHECK(generate(plan, src, pw));
    pw[0] = plan.classes[2].chars[(rng.next() >> 56) % 10];  // a digit always first
  }
  std::fprintf(stderr, "(expected) ");
  CHECK(!noPositionalBias(plan, placed));
}

}  // namespace

int main() {
  validation();
  acceptanceMatchesEnumeration();
  entropy();
  edgeLengthsAndMinimumsAtTheLimit();
  rejectionAndRngFailure();
  statistics();
  return KEYRA_TEST_RESULT();
}
