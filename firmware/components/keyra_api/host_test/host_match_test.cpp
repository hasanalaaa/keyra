// Host rule (SPEC §9.4): every row of the shared table in
// docs/research/HOST-MATCH.md, read from the file itself so the device can
// never drift from the extension and the Android app.
#include <fstream>
#include <string>
#include <vector>

#include "host_match.hpp"
#include "keyra_test.hpp"

using namespace keyra::api::hostmatch;

namespace {

struct Row {
  std::string login, page;
  bool match = false;
};

std::string trim(std::string s) {
  while (!s.empty() && s.front() == ' ') s.erase(0, 1);
  while (!s.empty() && s.back() == ' ') s.pop_back();
  return s;
}

// "`github.com`" → "github.com"; "*(empty)*" → "".
std::string cell(const std::string& raw) {
  const std::string s = trim(raw);
  if (s == "*(empty)*") return {};
  if (s.size() >= 2 && s.front() == '`' && s.back() == '`') return s.substr(1, s.size() - 2);
  return "\x01unparsed:" + s;  // never a host: fails the row loudly
}

std::vector<Row> readTable(const char* path) {
  std::vector<Row> rows;
  std::ifstream in(path);
  std::string line;
  bool inCases = false;
  while (std::getline(in, line)) {
    if (line.rfind("## ", 0) == 0) inCases = line == "## Cases";
    if (!inCases || line.rfind("| ", 0) != 0) continue;
    std::vector<std::string> cells;
    size_t start = 1;
    for (size_t bar; (bar = line.find('|', start)) != std::string::npos; start = bar + 1)
      cells.push_back(line.substr(start, bar - start));
    if (cells.size() != 3 || trim(cells[0]) == "Login host") continue;  // the header
    const std::string verdict = trim(cells[2]);
    CHECK(verdict == "yes" || verdict == "no");
    rows.push_back({cell(cells[0]), cell(cells[1]), verdict == "yes"});
  }
  return rows;
}

void sharedTable() {
  const std::vector<Row> rows = readTable(KEYRA_HOST_MATCH_MD);
  CHECK(rows.size() >= 17);  // the table was read (17 rows when this test was written)
  for (const Row& r : rows) {
    const bool got = matches(r.login, r.page);
    if (got != r.match) std::fprintf(stderr, "row: '%s' on '%s'\n", r.login.c_str(), r.page.c_str());
    CHECK(got == r.match);
  }
}

void normalising() {
  CHECK(normalize("  WWW.GitHub.com.  ") == "github.com");
  CHECK(normalize("www.www.example") == "www.example");  // one "www." only
  CHECK(normalize("github.com..") == "github.com.");     // one trailing dot only
  CHECK(normalize("") == "" && normalize("   ") == "");
  CHECK(normalize("www.") == "www");  // the trailing dot goes first: no "www." left to drop
}

void moreCases() {
  CHECK(matches("gist.github.com", "github.com"));  // either way round
  CHECK(matches("www.github.com", "github.com"));
  CHECK(!matches("   ", "   "));                    // empty after normalising
  CHECK(!matches("github.com", ""));
  CHECK(matches("::1", "::1") && !matches("::1", "x.::1"));
  CHECK(!matches("1.2.3.4", "a.1.2.3.4"));
  CHECK(matches("example.com.au", "shop.example.com.au"));
  CHECK(!matches("com.au", "example.com.au"));
  CHECK(matches("example.uk", "a.example.uk"));         // a two-label site under a ccTLD
  CHECK(matches("co.example", "a.co.example"));         // "co" under a non-country TLD
  CHECK(!matches("a..b", "x.a..b"));                    // empty label: not site-like
  CHECK(!matches("github.com", "agithub.com"));
}

void pageHosts() {
  CHECK(validHost("github.com") && validHost(" github.com ") && validHost("[::1]") && validHost("xn--mgbh0fb.example"));
  CHECK(validHost(std::string(kMaxHost, 'a')) && !validHost(std::string(kMaxHost + 1, 'a')));
  CHECK(!validHost("") && !validHost("   ") && !validHost("a b") && !validHost("a\tb") && !validHost("a\nb"));
  CHECK(!validHost("\xD9\x88.example") && !validHost(std::string("a\0b", 3)) && !validHost("a\x7f"));
}

}  // namespace

int main() {
  pageHosts();
  sharedTable();
  normalising();
  moreCases();
  return KEYRA_TEST_RESULT();
}
