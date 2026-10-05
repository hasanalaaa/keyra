#pragma once
// cJSON helpers. Every cJSON allocation is zeroised on free (see json.cpp), so
// secrets that pass through request/response JSON do not linger in the heap.
#include <cstdint>
#include <memory>
#include <string>

#include "cJSON.h"

namespace keyra::api::json {

void installWipingAllocator();

struct Deleter {
  void operator()(cJSON* j) const { cJSON_Delete(j); }
};
using Ptr = std::unique_ptr<cJSON, Deleter>;

enum class Field { Missing, Ok, BadType };
Field getString(const cJSON* obj, const char* key, std::string& out);
Field getBool(const cJSON* obj, const char* key, bool& out);
// Integral JSON numbers only (no fraction), within [min, max].
Field getInt(const cJSON* obj, const char* key, int64_t min, int64_t max, int64_t& out);

// A std::string that is wiped when it goes out of scope.
struct Secret {
  std::string s;
  Secret() = default;
  Secret(const Secret&) = delete;
  Secret& operator=(const Secret&) = delete;
  ~Secret();
};

}  // namespace keyra::api::json
