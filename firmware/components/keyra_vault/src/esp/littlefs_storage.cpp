#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <memory>

#include "esp_adapters.hpp"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_partition.h"

namespace keyra::vault::esp {
namespace {

constexpr char kTag[] = "vault.fs";
constexpr char kLabel[] = "vault";
constexpr char kBase[] = "/vault";

std::string full(const std::string& path) {
  return path.empty() ? std::string(kBase) : std::string(kBase) + "/" + path;
}

const esp_partition_t* partition() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, kLabel);
}

// True only if every byte is erased (0xFF): the one case where formatting
// cannot destroy anything.
bool partitionBlank(const esp_partition_t* p) {
  constexpr size_t kChunk = 4096;
  std::unique_ptr<uint8_t[]> buf(new uint8_t[kChunk]);
  for (size_t off = 0; off < p->size; off += kChunk) {
    size_t n = std::min(kChunk, size_t(p->size - off));
    if (esp_partition_read(p, off, buf.get(), n) != ESP_OK) return false;
    for (size_t i = 0; i < n; ++i)
      if (buf[i] != 0xFF) return false;
  }
  return true;
}

esp_err_t registerFs() {
  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = kBase;
  conf.partition_label = kLabel;
  conf.format_if_mount_failed = false;  // never silently format: see mount()
  return esp_vfs_littlefs_register(&conf);
}

using File = std::unique_ptr<FILE, int (*)(FILE*)>;

}  // namespace

bool LittleFsStorage::mount() {
  if (mounted_) return true;
  const esp_partition_t* p = partition();
  if (!p) {
    ESP_LOGE(kTag, "no data partition labelled '%s'", kLabel);
    return false;
  }
  esp_err_t err = registerFs();
  if (err == ESP_FAIL) {  // littlefs could not mount what is on the partition
    if (!partitionBlank(p)) {
      ESP_LOGE(kTag, "vault partition holds data littlefs cannot mount; refusing to format "
                     "(factory reset erases it)");
      return false;
    }
    ESP_LOGW(kTag, "vault partition is blank; formatting");
    err = esp_littlefs_format(kLabel);
    if (err == ESP_OK) err = registerFs();
  }
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "mount failed: %s", esp_err_to_name(err));
    return false;
  }
  mounted_ = true;
  if (::mkdir(full("e").c_str(), 0755) != 0 && errno != EEXIST) {
    ESP_LOGE(kTag, "mkdir e: errno %d", errno);
    return false;
  }
  return true;
}

Storage::Read LittleFsStorage::read(const std::string& path, std::vector<uint8_t>& out) {
  out.clear();
  File f(std::fopen(full(path).c_str(), "rb"), &std::fclose);
  if (!f) return errno == ENOENT ? Read::NotFound : Read::Error;
  struct stat st;
  if (::fstat(fileno(f.get()), &st) != 0 || st.st_size < 0) return Read::Error;
  out.resize(size_t(st.st_size));
  if (!out.empty() && std::fread(out.data(), 1, out.size(), f.get()) != out.size()) return Read::Error;
  return Read::Ok;
}

bool LittleFsStorage::write(const std::string& path, const uint8_t* data, size_t n) {
  FILE* f = std::fopen(full(path).c_str(), "wb");
  if (!f) return false;
  bool ok = (n == 0 || std::fwrite(data, 1, n, f) == n) && std::fflush(f) == 0 &&
            ::fsync(fileno(f)) == 0;
  // close() commits the littlefs file; its result matters as much as the write's.
  ok = std::fclose(f) == 0 && ok;
  return ok;
}

bool LittleFsStorage::rename(const std::string& from, const std::string& to) {
  // lfs_rename replaces an existing target atomically.
  return ::rename(full(from).c_str(), full(to).c_str()) == 0;
}

bool LittleFsStorage::remove(const std::string& path) {
  return ::unlink(full(path).c_str()) == 0 || errno == ENOENT;
}

bool LittleFsStorage::list(const std::string& dir, std::vector<std::string>& names) {
  names.clear();
  DIR* d = ::opendir(full(dir).c_str());
  if (!d) return errno == ENOENT;
  while (struct dirent* e = ::readdir(d)) {
    if (e->d_type == DT_REG) names.emplace_back(e->d_name);
  }
  ::closedir(d);
  return true;
}

bool LittleFsStorage::format() {
  const esp_partition_t* p = partition();
  if (!p) return false;
  if (mounted_) {
    esp_vfs_littlefs_unregister(kLabel);
    mounted_ = false;
  }
  // Erase every sector, not just the filesystem metadata: deleted files and
  // superseded meta.bin copies (old wrapped keys) must not survive a reset.
  esp_err_t err = esp_partition_erase_range(p, 0, p->size);
  if (err == ESP_OK) err = esp_littlefs_format(kLabel);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "format failed: %s", esp_err_to_name(err));
    return false;
  }
  return mount();
}

}  // namespace keyra::vault::esp
