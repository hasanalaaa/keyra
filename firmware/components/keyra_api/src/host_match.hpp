#pragma once
// Which stored login is offered on which page (SPEC §9.4). One rule shared with
// the browser extension and the Android app; the cases all three run are in
// docs/research/HOST-MATCH.md. Pure, host-tested.
#include <cstddef>
#include <string>
#include <string_view>

namespace keyra::api::hostmatch {

// Lowercased, surrounding spaces and one trailing '.' trimmed, one leading
// "www." dropped.
std::string normalize(std::string_view host);
// True when a login for `login` may be offered on (or typed into) `page`: the
// same host, or one is a subdomain of a site-like other. IP addresses match
// only exactly; an empty host matches nothing.
bool matches(std::string_view login, std::string_view page);
// A page host as a client may send it: 1–kMaxHost bytes of visible ASCII (a
// browser's URL.hostname is punycode), surrounding spaces allowed.
constexpr size_t kMaxHost = 253;
bool validHost(std::string_view host);

}  // namespace keyra::api::hostmatch
