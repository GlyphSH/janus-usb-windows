#include "event_log.h"

#include <cstdio>
#include <sys/stat.h>

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

namespace {
constexpr int kSdClock = 12;
constexpr int kSdCommand = 16;
constexpr int kSdD0 = 14;
constexpr int kSdD1 = 17;
constexpr int kSdD2 = 21;
constexpr int kSdD3 = 18;
constexpr size_t kMaxLogBytes = 64 * 1024;
constexpr const char *kMount = "/sd";
constexpr const char *kLogPath = "/sd/janus-events.log";
constexpr const char *kOldLogPath = "/sd/janus-events.previous.log";
constexpr const char *TAG = "event_log";
}  // namespace

uint32_t EventLog::now_ms() const {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

bool EventLog::begin() {
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.clk = static_cast<gpio_num_t>(kSdClock);
  slot.cmd = static_cast<gpio_num_t>(kSdCommand);
  slot.d0 = static_cast<gpio_num_t>(kSdD0);
  slot.d1 = static_cast<gpio_num_t>(kSdD1);
  slot.d2 = static_cast<gpio_num_t>(kSdD2);
  slot.d3 = static_cast<gpio_num_t>(kSdD3);
  slot.width = 4;

  esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {};
  mount_cfg.format_if_mount_failed = false;
  mount_cfg.max_files = 4;
  mount_cfg.allocation_unit_size = 16 * 1024;

  sdmmc_card_t *card = nullptr;
  esp_err_t err = esp_vfs_fat_sdmmc_mount(kMount, &host, &slot, &mount_cfg, &card);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "sd mount failed: %d (event log disabled)", err);
    ready_ = false;
    return false;
  }
  ready_ = true;
  last_flush_ms_ = now_ms();
  record("BOOT", "sd-ready");
  return true;
}

void EventLog::record(const char *event, const char *detail) {
  if (!ready_) return;
  char header[24];
  std::snprintf(header, sizeof(header), "%u,", static_cast<unsigned>(now_ms()));
  pending_ += header;
  pending_ += event;
  if (detail && detail[0] != '\0') {
    pending_ += ',';
    pending_ += detail;
  }
  pending_ += '\n';
  ++pending_events_;
  if (pending_events_ >= 4) flush();
}

bool EventLog::append_file(const char *path, const std::string &data) {
  FILE *f = std::fopen(path, "ab");
  if (!f) return false;
  size_t written = std::fwrite(data.data(), 1, data.size(), f);
  std::fclose(f);
  return written == data.size();
}

void EventLog::flush() {
  if (!ready_ || pending_.empty()) return;
  struct stat st{};
  if (::stat(kLogPath, &st) == 0 && st.st_size + static_cast<off_t>(pending_.size()) > static_cast<off_t>(kMaxLogBytes)) {
    ::remove(kOldLogPath);
    ::rename(kLogPath, kOldLogPath);
  }
  if (append_file(kLogPath, pending_)) {
    pending_.clear();
    pending_events_ = 0;
    last_flush_ms_ = now_ms();
  }
}

void EventLog::tick() {
  if (ready_ && now_ms() - last_flush_ms_ >= 5000) flush();
}
