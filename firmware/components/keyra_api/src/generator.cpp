#include "generator.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace keyra::gen {
namespace {

// Volatile stores: the compiler may not drop them as dead, unlike a plain memset
// of a buffer that is about to go out of scope.
void zero(void* p, size_t n) {
  volatile uint8_t* b = static_cast<volatile uint8_t*>(p);
  while (n--) *b++ = 0;
}

bool isSymbol(char c) {
  const auto u = static_cast<unsigned char>(c);
  const bool alnum = (u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z');
  return u >= 0x21 && u <= 0x7E && !alnum;
}

bool validSymbolSet(const std::string& s) {
  if (s.empty() || s.size() > kMaxSymbolSet) return false;
  for (size_t i = 0; i < s.size(); ++i) {
    if (!isSymbol(s[i]) || s.find(s[i]) != i) return false;
  }
  return true;
}

std::string without(const char* chars, const char* drop) {
  std::string out;
  for (const char* c = chars; *c; ++c)
    if (!std::strchr(drop, *c)) out += *c;
  return out;
}

std::string keepOnly(const std::string& chars, const std::string& allowed) {
  std::string out;
  for (char c : chars)
    if (allowed.find(c) != std::string::npos) out += c;
  return out;
}

// Draws bytes from the injected source in small batches and wipes them after,
// since they decide the password.
class ByteStream {
 public:
  explicit ByteStream(const Random& r) : random_(r) {}
  ~ByteStream() { zero(buf_, sizeof buf_); }
  bool next(uint8_t& out) {
    if (pos_ == sizeof buf_) {
      if (!random_(buf_, sizeof buf_)) return false;
      pos_ = 0;
    }
    out = buf_[pos_];
    buf_[pos_++] = 0;
    return true;
  }

 private:
  const Random& random_;
  uint8_t buf_[64] = {};
  size_t pos_ = sizeof buf_;
};

}  // namespace

Error plan(const Params& p, Plan& out) {
  out = Plan{};
  if (p.length < kMinLength || p.length > kMaxLength) return Error::Length;
  if (!p.lower && !p.upper && !p.digits && !p.symbols) return Error::NoClass;
  if (p.minDigits < 0 || p.minSymbols < 0 || p.minDigits > p.length || p.minSymbols > p.length)
    return Error::Minimum;
  if ((!p.digits && p.minDigits > 0) || (!p.symbols && p.minSymbols > 0)) return Error::Minimum;
  if (!p.symbolSet.empty() && !validSymbolSet(p.symbolSet)) return Error::SymbolSet;

  const std::string symbols = p.symbolSet.empty() ? kDefaultSymbols : p.symbolSet;
  struct Want {
    bool on;
    const char* chars;
    int min;
  } wants[] = {
      {p.lower, "abcdefghijklmnopqrstuvwxyz", 1},
      {p.upper, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 1},
      {p.digits, "0123456789", std::max(1, p.minDigits)},
      {p.symbols, symbols.c_str(), std::max(1, p.minSymbols)},
  };
  int floors = 0;
  std::memset(out.classOf, -1, sizeof out.classOf);
  for (const Want& w : wants) {
    if (!w.on) continue;
    Plan::Class& c = out.classes[out.classCount];
    c.chars = without(w.chars, p.avoidAmbiguous ? kAmbiguous : "");
    if (p.restrict) c.chars = keepOnly(c.chars, p.allowed);
    c.min = w.min;
    if (c.chars.empty()) return Error::EmptyClass;
    for (char ch : c.chars) out.classOf[static_cast<unsigned char>(ch)] = static_cast<int8_t>(out.classCount);
    out.alphabet += c.chars;
    floors += c.min;
    ++out.classCount;
  }
  if (floors > p.length) return Error::Minimum;
  out.length = p.length;
  out.acceptance = acceptance(out);
  if (out.acceptance < kMinAcceptance) return Error::TooStrict;
  return Error::None;
}

double acceptance(const Plan& plan) {
  // P = L! · [x^L] Π_i Σ_{c ≥ min_i} (q_i x)^c / c!   with q_i = |class i| / |alphabet|.
  const int L = plan.length;
  const double n = static_cast<double>(plan.alphabet.size());
  if (L <= 0 || n == 0) return 0;
  std::vector<double> acc(L + 1, 0.0), term(L + 1), next(L + 1);
  acc[0] = 1;
  for (int k = 0; k < plan.classCount; ++k) {
    const double q = static_cast<double>(plan.classes[k].chars.size()) / n;
    double t = 1;
    for (int c = 0; c <= L; ++c) {
      if (c > 0) t *= q / c;
      term[c] = c >= plan.classes[k].min ? t : 0;
    }
    std::fill(next.begin(), next.end(), 0.0);
    for (int a = 0; a <= L; ++a) {
      if (acc[a] == 0) continue;
      for (int c = 0; a + c <= L; ++c) next[a + c] += acc[a] * term[c];
    }
    acc.swap(next);
  }
  double factorial = 1;
  for (int i = 2; i <= L; ++i) factorial *= i;
  return std::min(1.0, acc[L] * factorial);
}

double entropyBits(const Plan& plan) {
  if (plan.acceptance <= 0) return 0;
  return plan.length * std::log2(static_cast<double>(plan.alphabet.size())) + std::log2(plan.acceptance);
}

bool generate(const Plan& plan, const Random& random, std::string& out) {
  const size_t n = plan.alphabet.size();
  if (plan.length <= 0 || n == 0 || n > 256) return false;
  // Largest multiple of n that fits in a byte: values at or above it are redrawn.
  const unsigned limit = 256 - 256 % n;
  ByteStream bytes(random);
  out.assign(static_cast<size_t>(plan.length), '\0');  // same buffer for every candidate
  for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
    int counts[4] = {};
    bool rngOk = true;
    for (int i = 0; i < plan.length && rngOk; ++i) {
      uint8_t b = 0;
      do {
        rngOk = bytes.next(b);
      } while (rngOk && b >= limit);
      const char c = plan.alphabet[b % n];
      out[static_cast<size_t>(i)] = c;
      ++counts[plan.classOf[static_cast<unsigned char>(c)]];
    }
    if (!rngOk) break;
    bool ok = true;
    for (int k = 0; k < plan.classCount; ++k) ok &= counts[k] >= plan.classes[k].min;
    if (ok) return true;
  }
  zero(&out[0], out.size());
  out.clear();
  return false;
}

const char* message(Error e) {
  switch (e) {
    case Error::None: return "ok";
    case Error::Length: return "length must be 8-128";
    case Error::NoClass: return "enable at least one of lower, upper, digits, symbols";
    case Error::Minimum: return "minDigits/minSymbols need their class enabled and must fit in the length";
    case Error::SymbolSet: return "symbolSet must be 1-32 distinct ASCII punctuation characters";
    case Error::EmptyClass: return "avoidAmbiguous or layoutSafe leaves an enabled class without characters";
    case Error::TooStrict: return "minimums are too high for this length";
  }
  return "invalid";
}

}  // namespace keyra::gen
