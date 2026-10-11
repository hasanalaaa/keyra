#include "backup_stream.hpp"

#include <algorithm>
#include <cstring>

#include "entry_codec.hpp"
#include "json.hpp"
#include "secure_buf.hpp"

namespace keyra::vault::backup {
namespace {

constexpr size_t kMaxEnvelopeBytes = 8 * 1024 * 1024;
constexpr size_t kMaxEntryJson = 64 * 1024;
constexpr size_t kMaxPasskeysJson = 128 * 1024;
constexpr char kDataKey[] = "\"data\":\"";

bool space(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

// The old exporter emits entries first, followed by the optional passkeys
// member. Only one entry JSON object is held while validating the stream.
class PlainReader {
 public:
  PlainReader(int64_t version, size_t maxEntries, Entries& entries, Passkeys& passkeys)
      : version_(version), maxEntries_(maxEntries), entries_(entries), passkeys_(passkeys) {}
  ~PlainReader() { wipe(item_); }

  bool write(const uint8_t* bytes, size_t n) {
    for (size_t i = 0; i < n; ++i)
      if (!step(char(bytes[i]))) return false;
    return true;
  }
  bool finish() const { return state_ == State::Done; }
  bool full() const { return full_; }

 private:
  enum class State { Prefix, Entries, InEntry, AfterEntry, AfterEntries, PasskeysKey,
                     PasskeysObject, AfterPasskeys, Done };
  bool capture(char c, size_t max) {
    if (item_.size() == max) return false;
    item_ += c;
    if (inString_) {
      if (escaped_) escaped_ = false;
      else if (c == '\\') escaped_ = true;
      else if (c == '"') inString_ = false;
    } else if (c == '"') {
      inString_ = true;
    } else if (c == '{' || c == '[') {
      ++depth_;
    } else if (c == '}' || c == ']') {
      if (--depth_ < 0) return false;
    }
    return true;
  }
  bool entryDone() {
    json::Value value;
    Entry e;
    const bool ok = json::parse(item_.data(), item_.size(), value) &&
                    readEntry(value, e) && codec::valid(e);
    wipe(item_);
    if (!ok) {
      wipe(e);
      return false;
    }
    if (entries_.size() == maxEntries_) {
      full_ = true;
      wipe(e);
    } else {
      entries_.push_back(std::move(e));
    }
    return true;
  }
  bool passkeysDone() {
    json::Value value;
    const bool ok = json::parse(item_.data(), item_.size(), value) && readPasskeys(value, passkeys_);
    wipe(item_);
    return ok;
  }
  bool step(char c) {
    if (state_ == State::InEntry || state_ == State::PasskeysObject) {
      if (!capture(c, state_ == State::InEntry ? kMaxEntryJson : kMaxPasskeysJson)) return false;
      if (depth_ != 0) return true;
      const bool entry = state_ == State::InEntry;
      if (!(entry ? entryDone() : passkeysDone())) return false;
      state_ = entry ? State::AfterEntry : State::AfterPasskeys;
      return true;
    }
    if (space(c)) return true;
    switch (state_) {
      case State::Prefix: {
        const char* expected = version_ >= 3 ? "{\"entries\":[" : "[";
        if (c != expected[prefixPos_++]) return false;
        if (expected[prefixPos_] == '\0') state_ = State::Entries;
        return true;
      }
      case State::Entries:
        if (c == ']') { state_ = version_ >= 3 ? State::AfterEntries : State::Done; return true; }
        if (c != '{') return false;
        item_.clear();
        depth_ = 0;
        inString_ = escaped_ = false;
        state_ = State::InEntry;
        return capture(c, kMaxEntryJson);
      case State::AfterEntry:
        if (c == ',') { state_ = State::Entries; return true; }
        if (c == ']') { state_ = version_ >= 3 ? State::AfterEntries : State::Done; return true; }
        return false;
      case State::AfterEntries:
        if (version_ < 3) return false;
        if (c == '}') { state_ = State::Done; return true; }
        if (c == ',') { prefixPos_ = 0; state_ = State::PasskeysKey; return true; }
        return false;
      case State::PasskeysKey: {
        constexpr char key[] = "\"passkeys\":{";
        if (c != key[prefixPos_]) {
          // prefixPos_ is reset by the first byte of this key below.
          return false;
        }
        if (key[++prefixPos_] == '\0') {
          state_ = State::PasskeysObject;
          item_ = "{";
          depth_ = 1;
          inString_ = escaped_ = false;
        }
        return true;
      }
      case State::AfterPasskeys:
        if (c == '}') { state_ = State::Done; return true; }
        return false;
      case State::PasskeysObject: case State::InEntry: return false;
      case State::Done: return false;
    }
    return false;
  }
  int64_t version_;
  size_t maxEntries_;
  Entries& entries_;
  Passkeys& passkeys_;
  State state_ = State::Prefix;
  size_t prefixPos_ = 0;
  int depth_ = 0;
  bool inString_ = false, escaped_ = false, full_ = false;
  SecureString item_;
};

int sextet(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

void wipePasskeys(Passkeys& passkeys) {
  for (auto& key : passkeys.keys) mem::zeroize(key.data(), key.size());
  for (auto& record : passkeys.records) {
    if (!record.empty()) mem::zeroize(record.data(), record.size());
  }
  passkeys = Passkeys{};
}

struct Pending {
  Entries entries;
  Passkeys passkeys;
  ~Pending() {
    for (auto& e : entries) wipe(e);
    wipePasskeys(passkeys);
  }
};

}  // namespace

Status readStream(Crypto& crypto, const std::string& passphrase, const Source& source, size_t maxEntries,
                  Entries& entries, Passkeys& passkeys) {
  Pending pending;
  SecureBuf chunk, cipherBuf, plainBuf;
  if (!chunk.alloc(4096) || !cipherBuf.alloc(4096) || !plainBuf.alloc(4096 + 16))
    return Status::StorageError;
  enum class State { Prefix, Data, Tail };
  State state = State::Prefix;
  std::string prefix;
  Envelope env;
  std::unique_ptr<Crypto::Aead> op;
  std::unique_ptr<PlainReader> plain;
  uint8_t held[16] = {};
  size_t cipherLen = 0;
  size_t heldLen = 0, quartetLen = 0, count = 0;
  char quartet[4] = {};
  bool padded = false, closed = false;
  bool parseInvalid = false;
  auto decrypted = [&](const uint8_t* data, size_t n) -> Status {
    uint8_t* out = plainBuf.data();
    while (n) {
      const size_t take = std::min<size_t>(n, 4096);
      size_t produced = 0;
      if (!op->update(data, take, out, produced)) {
        mem::zeroize(out, plainBuf.size());
        return Status::StorageError;
      }
      if (!parseInvalid && !plain->write(out, produced)) parseInvalid = true;
      mem::zeroize(out, plainBuf.size());
      data += take;
      n -= take;
    }
    return Status::Ok;
  };
  auto ciphertext = [&](const uint8_t* data, size_t n) -> Status {
    // GCM's final 16 bytes are the tag. Keep exactly those bytes out of update().
    for (size_t i = 0; i < n; ++i) {
      if (heldLen < sizeof held) {
        held[heldLen++] = data[i];
      } else {
        const uint8_t oldest = held[0];
        std::memmove(held, held + 1, sizeof held - 1);
        held[sizeof held - 1] = data[i];
        cipherBuf.data()[cipherLen++] = oldest;
        if (cipherLen == cipherBuf.size()) {
          if (Status s = decrypted(cipherBuf.data(), cipherLen); s != Status::Ok) return s;
          cipherLen = 0;
        }
      }
    }
    return Status::Ok;
  };
  for (;;) {
    size_t got = 0;
    if (!source(reinterpret_cast<char*>(chunk.data()), chunk.size(), got) || got > chunk.size())
      return Status::StorageError;
    if (got == 0) break;
    count += got;
    if (count > kMaxEnvelopeBytes) return Status::Full;
    for (size_t i = 0; i < got; ++i) {
      const char c = char(chunk.data()[i]);
      if (state == State::Prefix) {
        if (prefix.size() == 2048) return Status::Invalid;
        prefix += c;
        if (prefix.size() < sizeof kDataKey - 1 ||
            prefix.compare(prefix.size() - (sizeof kDataKey - 1), sizeof kDataKey - 1, kDataKey) != 0)
          continue;
        // Reuse the original parser for all KDF and envelope validation. The
        // synthetic data member is discarded and never holds actual ciphertext.
        if (!readEnvelope(prefix + "AAAAAAAAAAAAAAAAAAAAAA==\"}", env)) return Status::Invalid;
        uint8_t key[32] = {};
        const bool derived = crypto.pbkdf2Sha256(passphrase, env.salt, sizeof env.salt,
                                                 env.iterations, key, sizeof key);
        if (derived) op = crypto.gcmStream(key, env.iv, false);
        mem::zeroize(key, sizeof key);
        if (!derived || !op) return Status::StorageError;
        pending.entries.reserve(maxEntries);  // no reallocation of secret strings
        plain = std::make_unique<PlainReader>(env.version, maxEntries, pending.entries, pending.passkeys);
        prefix.clear();
        state = State::Data;
      } else if (state == State::Data) {
        if (c == '"') {
          if (quartetLen || heldLen != 16) return Status::Invalid;
          state = State::Tail;
          continue;
        }
        if (padded || (c != '=' && sextet(c) < 0)) return Status::Invalid;
        quartet[quartetLen++] = c;
        if (quartetLen == 4) {
          if (quartet[0] == '=' || quartet[1] == '=' || (quartet[2] == '=' && quartet[3] != '='))
            return Status::Invalid;
          padded = quartet[2] == '=' || quartet[3] == '=';
          uint32_t x = 0;
          for (char q : quartet) x = (x << 6) | uint32_t(q == '=' ? 0 : sextet(q));
          uint8_t decoded[3] = {uint8_t(x >> 16), uint8_t(x >> 8), uint8_t(x)};
          const size_t n = quartet[2] == '=' ? 1 : quartet[3] == '=' ? 2 : 3;
          if (Status s = ciphertext(decoded, n); s != Status::Ok) return s;
          quartetLen = 0;
        }
      } else if (c == '}') {
        if (closed) return Status::Invalid;
        closed = true;
      } else if (!space(c) || !closed) {
        return Status::Invalid;
      }
    }
  }
  if (state != State::Tail || !closed || heldLen != 16) return Status::Invalid;
  if (cipherLen) {
    if (Status s = decrypted(cipherBuf.data(), cipherLen); s != Status::Ok) return s;
  }
  uint8_t out[16] = {};
  size_t produced = 0;
  const Crypto::Open opened = op->verify(held, out, produced);
  if (opened == Crypto::Open::AuthFailed) {
    mem::zeroize(out, sizeof out);
    return Status::WrongPassphrase;
  }
  if (opened != Crypto::Open::Ok) {
    mem::zeroize(out, sizeof out);
    return Status::StorageError;
  }
  const bool parsed = !parseInvalid && plain->write(out, produced) && plain->finish();
  mem::zeroize(out, sizeof out);
  if (!parsed) return Status::Invalid;
  if (plain->full()) return Status::Full;
  for (auto& e : entries) wipe(e);
  entries = std::move(pending.entries);
  wipePasskeys(passkeys);
  passkeys = std::move(pending.passkeys);
  return Status::Ok;
}

}  // namespace keyra::vault::backup
