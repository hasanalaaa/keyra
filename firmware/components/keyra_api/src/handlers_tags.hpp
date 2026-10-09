#pragma once
// NFC tap tags (SPEC §18): /api/tags (a session manages them) and /api/tag/…
// (the tap page arms one account). Tag policy and SUN verification are in
// tags.hpp; this file does AES, hashing, storage, HTTP and logging.
#include <string>

#include "esp_http_server.h"
#include "json.hpp"

namespace keyra::api::tagapi {

esp_err_t listTags(httpd_req_t* r);                                              // GET /api/tags
esp_err_t createTag(httpd_req_t* r, const cJSON* body, const std::string& session);  // POST /api/tags
esp_err_t deleteTag(httpd_req_t* r, uint32_t id);                                // DELETE /api/tags/{id}
esp_err_t tap(httpd_req_t* r, const cJSON* body);                                // POST /api/tag/tap
esp_err_t status(httpd_req_t* r, const cJSON* body);                             // POST /api/tag/status

// The name of the tag behind a pending-machine owner, or empty when no tag armed it.
std::string ownerName(const std::string& owner);

}  // namespace keyra::api::tagapi
