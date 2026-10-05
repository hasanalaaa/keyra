#include "json.hpp"

#include <cmath>
#include <cstdlib>

#include "esp_heap_caps.h"
#include "keyra/vault.hpp"
#include "mbedtls/platform_util.h"

namespace keyra::api::json {
namespace {

void* jsonMalloc(size_t n) { return std::malloc(n); }

void jsonFree(void* p) {
  if (!p) return;
  mbedtls_platform_zeroize(p, heap_caps_get_allocated_size(p));
  std::free(p);
}

}  // namespace

void installWipingAllocator() {
  // Custom hooks also stop cJSON from using realloc, which could leave copies behind.
  cJSON_Hooks hooks{jsonMalloc, jsonFree};
  cJSON_InitHooks(&hooks);
}

Field getString(const cJSON* obj, const char* key, std::string& out) {
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (!v) return Field::Missing;
  if (!cJSON_IsString(v) || !v->valuestring) return Field::BadType;
  out.assign(v->valuestring);
  return Field::Ok;
}

Field getBool(const cJSON* obj, const char* key, bool& out) {
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (!v) return Field::Missing;
  if (!cJSON_IsBool(v)) return Field::BadType;
  out = cJSON_IsTrue(v);
  return Field::Ok;
}

Field getInt(const cJSON* obj, const char* key, int64_t min, int64_t max, int64_t& out) {
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (!v) return Field::Missing;
  if (!cJSON_IsNumber(v)) return Field::BadType;
  const double d = v->valuedouble;
  if (std::floor(d) != d || d < static_cast<double>(min) || d > static_cast<double>(max)) return Field::BadType;
  out = static_cast<int64_t>(d);
  return Field::Ok;
}

Secret::~Secret() { vault::wipe(s); }

}  // namespace keyra::api::json
