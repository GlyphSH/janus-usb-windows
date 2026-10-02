#pragma once

#include <stdint.h>

namespace janus {

enum class HostOs : uint8_t { Unknown, Windows, MacOS, Linux, Android };

struct UsbSetupTrace {
  // 64-bit microsecond timestamp; esp_timer_get_time() is already int64_t,
  // so widening here keeps the fingerprint span correct past the 71-minute
  // uint32_t wrap that an idle-then-replugged device would otherwise hit.
  uint64_t elapsed_us;
  uint8_t bm_request_type;
  uint8_t b_request;
  uint16_t w_value;
  uint16_t w_index;
  uint16_t w_length;
};

struct FingerprintResult {
  HostOs os;
  uint8_t confidence;
  bool saw_hid_led_report;
  uint16_t request_count;
};

class UsbFingerprint {
 public:
  void reset();
  void observe_setup(const UsbSetupTrace& trace);
  void observe_hid_led(uint8_t led_mask, uint64_t elapsed_us);
  FingerprintResult classify() const;

 private:
  uint64_t first_setup_us_ = 0;
  uint64_t last_setup_us_ = 0;
  uint16_t request_count_ = 0;
  uint16_t standard_get_descriptor_ = 0;
  uint16_t set_configuration_ = 0;
  uint16_t hid_set_report_ = 0;
  uint16_t hid_led_reports_ = 0;
  uint8_t last_led_mask_ = 0;
};

const char* host_os_name(HostOs os);

}  // namespace janus
