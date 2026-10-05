#pragma once
// esp_http_server response/request helpers shared by the API and static handlers.
#include <string>

#include "esp_http_server.h"
#include "json.hpp"

namespace keyra::api::http {

constexpr size_t kMaxBody = 64 * 1024;

constexpr const char* k200 = "200 OK";
constexpr const char* k201 = "201 Created";
constexpr const char* k202 = "202 Accepted";
constexpr const char* k204 = "204 No Content";
constexpr const char* k400 = "400 Bad Request";
constexpr const char* k401 = "401 Unauthorized";
constexpr const char* k403 = "403 Forbidden";
constexpr const char* k404 = "404 Not Found";
constexpr const char* k405 = "405 Method Not Allowed";
constexpr const char* k409 = "409 Conflict";
constexpr const char* k413 = "413 Payload Too Large";
constexpr const char* k429 = "429 Too Many Requests";
constexpr const char* k500 = "500 Internal Server Error";
constexpr const char* k503 = "503 Service Unavailable";
constexpr const char* k507 = "507 Insufficient Storage";

// CSP, X-Frame-Options, Referrer-Policy, nosniff (SPEC §6) on every response.
void securityHeaders(httpd_req_t* r);

esp_err_t sendJson(httpd_req_t* r, const char* status, const cJSON* body);
cJSON* errorBody(const char* code, const char* message);
esp_err_t sendError(httpd_req_t* r, const char* status, const char* code, const char* message);
esp_err_t sendEmpty(httpd_req_t* r, const char* status);

// Reads the whole body (≤ kMaxBody, checked by the caller) into `out`.
esp_err_t readBody(httpd_req_t* r, std::string& out);

// Request header value, or empty when absent/oversized.
std::string header(httpd_req_t* r, const char* name, size_t maxLen);

}  // namespace keyra::api::http
