#include <dirent.h>
#include <fcntl.h>
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

struct Fd {
  int fd;
  ~Fd() {
    if (fd >= 0) ::close(fd);
  }
};

}  // namespace

const esp_partition_t* LittleFsStorage::partition() const {
  return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label_);
}

esp_err_t LittleFsStorage::registerFs() const {
  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = base_.c_str();
  conf.partition_label = label_;
  conf.format_if_mount_failed = false;  // never silently format: see mount()
  return esp_vfs_littlefs_register(&conf);
}

bool LittleFsStorage::mount() {
  if (mounted_) return true;
  const esp_partition_t* p = partition();
  if (!p) {
    ESP_LOGE(kTag, "no data partition labelled '%s'", label_);
    return false;
  }
  esp_err_t err = registerFs();
  if (err == ESP_FAIL) {  // littlefs could not mount what is on the partition
    if (!partitionBlank(p)) {
      ESP_LOGE(kTag, "%s holds data littlefs cannot mount; refusing to format (factory reset erases it)", label_);
      return false;
    }
    ESP_LOGW(kTag, "%s is blank; formatting", label_);
    err = esp_littlefs_format(label_);
    if (err == ESP_OK) err = registerFs();
  }
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "mount %s failed: %s", label_, esp_err_to_name(err));
    return false;
  }
  mounted_ = true;
  for (const char* dir : dirs_) {
    if (::mkdir(full(dir).c_str(), 0755) != 0 && errno != EEXIST) {
      ESP_LOGE(kTag, "mkdir %s: errno %d", dir, errno);
      return false;
    }
  }
  return true;
}

Storage::Read LittleFsStorage::read(const std::string& path, std::vector<uint8_t>& out) {
  out.clear();
  // open/lseek/read, not fopen/fstat: esp_littlefs answers fstat with a fresh
  // lookup of the path, which walks the whole directory again — with thousands
  // of entries that doubled the flash reads of an unlock (docs/research/CAPACITY.md).
  // lseek works on the open file and costs no lookup.
  const Fd f{::open(full(path).c_str(), O_RDONLY)};
  const int fd = f.fd;
  if (fd < 0) return errno == ENOENT ? Read::NotFound : Read::Error;
  const off_t size = ::lseek(fd, 0, SEEK_END);
  if (size < 0 || ::lseek(fd, 0, SEEK_SET) != 0) return Read::Error;
  out.resize(size_t(size));
  for (size_t got = 0; got < out.size();) {
    const ssize_t n = ::read(fd, out.data() + got, out.size() - got);
    if (n <= 0) return Read::Error;  // a short file here means it changed under us
    got += size_t(n);
  }
  return Read::Ok;
}

bool LittleFsStorage::write(const std::string& path, const uint8_t* data, size_t n) {
  lastFailure_ = Failure::Other;
  FILE* f = std::fopen(full(path).c_str(), "wb");
  if (!f) {
    if (errno == ENOSPC) lastFailure_ = Failure::NoSpace;
    return false;
  }
  bool ok = true;
  int savedErrno = 0;
  if (n && std::fwrite(data, 1, n, f) != n) { ok = false; savedErrno = errno; }
  if (std::fflush(f) != 0) { ok = false; if (!savedErrno || errno == ENOSPC) savedErrno = errno; }
  if (::fsync(fileno(f)) != 0) { ok = false; if (!savedErrno || errno == ENOSPC) savedErrno = errno; }
  // close() commits the littlefs file; its result matters as much as the write's.
  if (std::fclose(f) != 0) { ok = false; if (!savedErrno || errno == ENOSPC) savedErrno = errno; }
  if (!ok && savedErrno == ENOSPC) lastFailure_ = Failure::NoSpace;
  return ok;
}

bool LittleFsStorage::rename(const std::string& from, const std::string& to) {
  // lfs_rename replaces an existing target atomically.
  lastFailure_ = Failure::Other;
  if (::rename(full(from).c_str(), full(to).c_str()) == 0) return true;
  if (errno == ENOSPC) lastFailure_ = Failure::NoSpace;
  return false;
}

bool LittleFsStorage::remove(const std::string& path) {
  lastFailure_ = Failure::Other;
  if (::unlink(full(path).c_str()) == 0 || errno == ENOENT) return true;
  if (errno == ENOSPC) lastFailure_ = Failure::NoSpace;
  return false;
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
    esp_vfs_littlefs_unregister(label_);
    mounted_ = false;
  }
  // Erase every sector, not just the filesystem metadata: deleted files and
  // superseded meta.bin copies (old wrapped keys) must not survive a reset.
  esp_err_t err = esp_partition_erase_range(p, 0, p->size);
  if (err == ESP_OK) err = esp_littlefs_format(label_);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "format %s failed: %s", label_, esp_err_to_name(err));
    return false;
  }
  return mount();
}

}  // namespace keyra::vault::esp
