// The vault's second home on boards with 16 MB of flash (docs/SPEC.md §6.1):
// the upper 8 MB, which the 8 MB partition table leaves unused, registered at
// run time as a data partition. Neither the partition table nor the
// bootloader change (neither is ever updated over the air).
#include "esp_adapters.hpp"
#include "esp_flash.h"
#include "esp_flash_chips/esp_flash_types.h"
#include "esp_log.h"
#include "esp_partition.h"

namespace keyra::vault::esp {
namespace {

constexpr char kTag[] = "vault.tail";
constexpr uint32_t kTailOffset = 0x800000, kTailSize = 0x800000, kFlashNeeded = kTailOffset + kTailSize;

// The one place Keyra departs from IDF's flash setup (docs/research/CAPACITY.md).
// IDF limits every esp_flash_* call to the flash size in the bootloader's image
// header (esp_flash_spi_init.c: "Detected size larger than the size in the
// binary image header ... Using the size in the binary image header"). Keyra
// builds with an 8 MB header so one image runs on 8 and 16 MB boards, and the
// bootloader is never updated over the air, so on a 16 MB board the driver
// stops at 8 MB. Only when the chip itself reports at least 16 MB, raise the
// driver's limit (esp_flash_t::size, "the available size") to 16 MB: nothing
// is mapped above 8 MB, and 16 MB needs no 4-byte addressing. Never beyond 16 MB.
bool allowFlashUpTo16MB(uint32_t physical) {
  esp_flash_t* chip = esp_flash_default_chip;
  if (chip == nullptr || physical < kFlashNeeded) return false;
  if (chip->size < kFlashNeeded) {
    ESP_LOGI(kTag, "flash chip has %u KiB, image header %u KiB: using the first 16 MiB",
             unsigned(physical / 1024), unsigned(chip->size / 1024));
    chip->size = kFlashNeeded;
  }
  uint32_t size = 0;
  return esp_flash_get_size(chip, &size) == ESP_OK && size >= kFlashNeeded;
}

const esp_partition_t* registerOnce() {
  uint32_t physical = 0;
  const esp_err_t err = esp_flash_get_physical_size(esp_flash_default_chip, &physical);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "flash size unknown (%s): the vault stays in its partition", esp_err_to_name(err));
    return nullptr;
  }
  if (physical < kFlashNeeded) return nullptr;  // an 8 MB board: nothing to do
  if (!allowFlashUpTo16MB(physical)) {
    ESP_LOGE(kTag, "could not open the upper 8 MiB: the vault stays in its partition");
    return nullptr;
  }
  const esp_partition_t* p = nullptr;
  esp_err_t e = esp_partition_register_external(nullptr, kTailOffset, kTailSize, kTailLabel,
                                                ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, &p);
  if (e != ESP_OK) {  // ESP_ERR_INVALID_ARG: a partition overlaps it
    ESP_LOGE(kTag, "registering the upper 8 MiB: %s", esp_err_to_name(e));
    return nullptr;
  }
  // The driver must now read there; if it does not, nothing may rely on it.
  uint8_t probe[16];
  e = esp_partition_read(p, kTailSize - sizeof probe, probe, sizeof probe);
  if (e != ESP_OK) {
    ESP_LOGE(kTag, "reading the upper 8 MiB: %s", esp_err_to_name(e));
    esp_partition_deregister_external(p);
    return nullptr;
  }
  ESP_LOGI(kTag, "upper 8 MiB registered as '%s'", kTailLabel);
  return p;
}

}  // namespace

const esp_partition_t* registerTail() {
  static const esp_partition_t* const tail = registerOnce();
  return tail;
}

}  // namespace keyra::vault::esp
