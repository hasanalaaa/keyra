// Auto-type sequence grammar (SPEC §10.4): a whitelist, so no import or
// restore can turn Keyra into a shortcut-pressing keyboard.
#include <string>
#include <vector>

#include "check.hpp"
#include "keyra/sequence.hpp"

using namespace keyra;
using seq::Error;
using seq::Kind;

static Error parse(const std::string& s) { return seq::parse(s, nullptr); }

TEST(sequence_accepts_the_grammar) {
  std::vector<seq::Step> st;
  CHECK(seq::parse("{USERNAME}{TAB}{PASSWORD}{ENTER}", &st) == Error::None);
  CHECK(st.size() == 4 && st[0].kind == Kind::Username && st[1].kind == Kind::Tab &&
        st[2].kind == Kind::Password && st[3].kind == Kind::Enter);
  CHECK(seq::parse("me@corp.com{TAB}{TOTP}{SPACE}x{{}y{}}z", &st) == Error::None);
  CHECK(st.size() == 5 && st[0].kind == Kind::Text && st[0].text == "me@corp.com" && st[2].kind == Kind::Totp &&
        st[4].kind == Kind::Text && st[4].text == "x{y}z");  // escapes merge into the literal
  CHECK(seq::parse("{PASSWORD}{DELAY 100}{DELAY 3000}{ENTER}", &st) == Error::None);
  CHECK(st[1].kind == Kind::Delay && st[1].ms == 100 && st[2].ms == 3000);
  CHECK(seq::parse("{USERNAME}{ENTER}{PRESS}{PASSWORD}{ENTER}", &st) == Error::None);
  CHECK(st[2].kind == Kind::Press);
  CHECK(parse("^v+a%b~") == Error::None);  // KeePass modifier characters are plain text here
  CHECK(parse("\xD9\x83\xD9\x84\xD9\x85\xD8\xA9") == Error::None);  // Arabic literal
  CHECK(seq::preview(st) == "{USERNAME}{ENTER}{PRESS}{PASSWORD}{ENTER}");
  CHECK(seq::parse("ab{TAB}\xD9\x83", &st) == Error::None);
  CHECK(seq::preview(st) == "\xE2\x80\xA2\xE2\x80\xA2{TAB}\xE2\x80\xA2");  // literals masked per character
}

TEST(sequence_rejects_chords_and_unknown_tokens) {
  for (const char* s : {"{CTRL}", "{CTRL+V}", "{ALT}", "{ALT+F4}", "{WIN}", "{LWIN}", "{CMD}", "{SHIFT}",
                        "{F1}", "{DELETE}", "{UP}", "{username}", "{USERNAME }", "{ USERNAME}", "{}", "{URL}",
                        "{NOTES}", "{S:pin}", "{VKEY 13}", "{TAB 3}"})
    CHECK(parse(s) == Error::Unknown);
  CHECK(parse("{TAB") == Error::Unclosed);
  CHECK(parse("a}b") == Error::Stray);
  CHECK(parse("a\nb") == Error::BadText);
  CHECK(parse("a\tb") == Error::BadText);
  CHECK(parse("a\x7F") == Error::BadText);
  CHECK(parse("\xC3\x28") == Error::BadText);
}

TEST(sequence_limits) {
  CHECK(parse("") == Error::Empty);
  CHECK(parse("{DELAY 500}") == Error::Empty);
  CHECK(parse(std::string(256, 'a')) == Error::None);
  CHECK(parse(std::string(257, 'a')) == Error::TooLong);
  for (const char* s : {"{DELAY 99}", "{DELAY 3001}", "{DELAY 0100}", "{DELAY }", "{DELAY -5}", "{DELAY 1e3}",
                        "{DELAY 100 }", "{DELAY  100}"})
    CHECK(parse(s) == Error::Delay);
  std::string many;
  for (int i = 0; i < 33; ++i) many += "{TAB}";
  CHECK(parse(many) == Error::TooManySteps);
  CHECK(parse("a{PRESS}b{PRESS}c{PRESS}d{PRESS}e") == Error::None);
  CHECK(parse("a{PRESS}b{PRESS}c{PRESS}d{PRESS}e{PRESS}f") == Error::TooManyPresses);
  CHECK(parse("{PRESS}{PASSWORD}") == Error::PressPlacement);
  CHECK(parse("{PASSWORD}{PRESS}") == Error::PressPlacement);
  CHECK(parse("{PASSWORD}{PRESS}{DELAY 200}{PRESS}{ENTER}") == Error::PressPlacement);
  CHECK(parse("a{DELAY 3000}{DELAY 3000}{DELAY 3000}{DELAY 1000}") == Error::None);
  CHECK(parse("a{DELAY 3000}{DELAY 3000}{DELAY 3000}{DELAY 1001}") == Error::TooMuchDelay);
  // Expansion: fields count at their maximum length (256 each).
  CHECK(parse("{PASSWORD}{PASSWORD}{PASSWORD}{PASSWORD}") == Error::None);
  CHECK(parse("{PASSWORD}{PASSWORD}{PASSWORD}{PASSWORD}x") == Error::TooMuchTyping);
  CHECK(seq::valid(""));
  CHECK(!seq::valid("{CTRL}"));
  std::vector<seq::Step> st{{Kind::Text, "keep?", 0}};
  CHECK(seq::parse("{CTRL}", &st) == Error::Unknown && st.size() == 1);  // untouched on failure
}

TEST_MAIN()
