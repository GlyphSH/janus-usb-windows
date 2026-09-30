#pragma once

#include <cstdint>

namespace janus {

struct ScreenState {
  bool usb_mounted;
  bool cdc_connected;
  bool hid_ready;
  const char *last_cdc_line;   // owned by caller, valid until next update
  const char *fp_os_name;
  uint8_t fp_confidence;
  bool fp_saw_hid_led;
  uint16_t fp_request_count;
  uint32_t hid_shortcuts;      // count of Win+R shortcuts fired
};

namespace screen {

bool begin();
void update(const ScreenState &s);

}  // namespace screen
}  // namespace janus
