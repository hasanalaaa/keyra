// The web app's packed /assets/ files (SPEC §5 Static): lookup and bounds checks.
#include <string>
#include <vector>

#include "asset_pack.hpp"
#include "keyra_test.hpp"

using namespace keyra::api::assets;

namespace {

void put16(std::vector<uint8_t>& b, size_t v) {
  b.push_back(uint8_t(v));
  b.push_back(uint8_t(v >> 8));
}

void add(std::vector<uint8_t>& b, const std::string& path, const std::string& type, uint8_t flags, const std::string& body) {
  put16(b, path.size());
  b.insert(b.end(), path.begin(), path.end());
  put16(b, type.size() + 1);
  b.insert(b.end(), type.begin(), type.end());
  b.push_back(0);
  b.push_back(flags);
  for (int i = 0; i < 4; i++) b.push_back(uint8_t(body.size() >> (8 * i)));
  b.insert(b.end(), body.begin(), body.end());
}

std::vector<uint8_t> sample() {
  std::vector<uint8_t> b{'K', 'Y', 'A', '1'};
  add(b, "/assets/index-AbC.js", "text/javascript; charset=utf-8", 1, "JS-GZ");
  add(b, "/assets/style-x.css", "text/css; charset=utf-8", 1, "");
  add(b, "/assets/font-Q.woff2", "font/woff2", 0, std::string(300, 'F'));
  return b;
}

std::string text(const File& f) { return std::string(reinterpret_cast<const char*>(f.body), f.size); }

}  // namespace

int main() {
  const std::vector<uint8_t> p = sample();

  auto js = find(p.data(), p.size(), "/assets/index-AbC.js");
  CHECK(js.has_value());
  CHECK(js && std::string(js->type) == "text/javascript; charset=utf-8");
  CHECK(js && js->gzip);
  CHECK(js && text(*js) == "JS-GZ");

  auto css = find(p.data(), p.size(), "/assets/style-x.css");
  CHECK(css && css->size == 0 && css->gzip);

  auto font = find(p.data(), p.size(), "/assets/font-Q.woff2");
  CHECK(font && !font->gzip && font->size == 300 && std::string(font->type) == "font/woff2");

  // Exact paths only: no prefixes, suffixes, other cases or query strings.
  for (const char* miss : {"/assets/index-AbC.j", "/assets/index-AbC.js2", "/ASSETS/index-AbC.js", "/assets/",
                           "assets/index-AbC.js", "", "/assets/index-AbC.js?x"})
    CHECK(!find(p.data(), p.size(), miss));

  // No pack, a wrong magic, or a pack cut anywhere finds nothing past the cut and never reads beyond it.
  CHECK(!find(nullptr, 0, "/assets/index-AbC.js"));
  std::vector<uint8_t> bad = p;
  bad[3] = '2';
  CHECK(!find(bad.data(), bad.size(), "/assets/index-AbC.js"));
  for (size_t cut = 0; cut < p.size(); cut++) {
    std::vector<uint8_t> part(p.begin(), p.begin() + cut);  // its own allocation: ASan catches overreads
    CHECK(!find(part.data(), part.size(), "/assets/font-Q.woff2"));
  }

  // A size running past the end is refused, not trusted.
  std::vector<uint8_t> big{'K', 'Y', 'A', '1'};
  add(big, "/assets/a.js", "text/javascript", 1, "abc");
  big[big.size() - 4] = 0xff;  // the size's high byte (body "abc" follows): ~4 GB
  big[big.size() - 5] = 0xff;
  big[big.size() - 6] = 0xff;
  big[big.size() - 7] = 0xff;
  CHECK(!find(big.data(), big.size(), "/assets/a.js"));

  // The type must be a single C string: no missing terminator, no embedded NUL, not empty.
  std::vector<uint8_t> t1{'K', 'Y', 'A', '1'};
  add(t1, "/a", "text/css", 1, "x");
  t1[4 + 2 + 2 + 2 + 8] = 'X';  // the type's NUL
  CHECK(!find(t1.data(), t1.size(), "/a"));
  std::vector<uint8_t> t2{'K', 'Y', 'A', '1'};
  add(t2, "/a", "text/css", 1, "x");
  t2[4 + 2 + 2 + 2 + 3] = 0;
  CHECK(!find(t2.data(), t2.size(), "/a"));
  std::vector<uint8_t> t3{'K', 'Y', 'A', '1'};
  add(t3, "/a", "", 1, "x");
  CHECK(!find(t3.data(), t3.size(), "/a"));

  return KEYRA_TEST_RESULT();
}
