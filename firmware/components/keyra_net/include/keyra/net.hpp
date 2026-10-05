#pragma once
// SoftAP, captive DNS and mDNS for Keyra (SPEC §4.1, §5 "Captive / connectivity").
#include <cstdint>
#include <string>

#include "esp_err.h"

namespace keyra::net {

struct Config { std::string ssid, password; uint8_t channel = 6; };

esp_err_t start(const Config&);    // single clean AP bring-up (config set before start), DNS, mDNS keyra.local
esp_err_t reconfigure(const Config&);
int stations();

}  // namespace keyra::net
