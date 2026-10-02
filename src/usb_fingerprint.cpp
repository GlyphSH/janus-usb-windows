#include "usb_fingerprint.h"

namespace janus {

void UsbFingerprint::reset() { *this = UsbFingerprint{}; }

void UsbFingerprint::observe_setup(const UsbSetupTrace& t) {
  if (request_count_ == 0) first_setup_us_ = t.elapsed_us;
  last_setup_us_ = t.elapsed_us;
  if (request_count_ != UINT16_MAX) ++request_count_;
  // GET_DESCRIPTOR is a standard device request (direction is intentionally
  // ignored because hosts differ in how they encode the setup packet).
  if ((t.bm_request_type & 0x60u) == 0 && t.b_request == 0x06u &&
      standard_get_descriptor_ != UINT16_MAX) ++standard_get_descriptor_;
  if ((t.bm_request_type & 0x60u) == 0 && t.b_request == 0x09u &&
      set_configuration_ != UINT16_MAX) ++set_configuration_;
  // HID SET_REPORT, class/interface OUT request.
  if ((t.bm_request_type & 0x7fu) == 0x21u && t.b_request == 0x09u &&
      hid_set_report_ != UINT16_MAX) ++hid_set_report_;
}

void UsbFingerprint::observe_hid_led(uint8_t led_mask, uint64_t elapsed_us) {
  last_led_mask_ = led_mask;
  ++hid_led_reports_;
  observe_setup(UsbSetupTrace{elapsed_us, 0x21, 0x09, 0x0200, 0, 1});
}

FingerprintResult UsbFingerprint::classify() const {
  FingerprintResult r{HostOs::Unknown, 0, hid_led_reports_ != 0, request_count_};
  if (request_count_ == 0) return r;

  // These are deliberately conservative signals. Enumeration timing alone is
  // never treated as proof; it only contributes when combined with request
  // shape. The observer records raw traces so signatures can be improved from
  // captures without changing the USB transport.
  const uint64_t span = last_setup_us_ - first_setup_us_;
  if (hid_led_reports_ != 0) {
    r.os = HostOs::Windows;
    r.confidence = (span < 2000000u && standard_get_descriptor_ >= 3) ? 70 : 45;
  } else if (set_configuration_ != 0 && standard_get_descriptor_ >= 2) {
    r.os = HostOs::Unknown;
    r.confidence = 10;
  }
  (void)last_led_mask_;
  (void)hid_set_report_;
  return r;
}

const char* host_os_name(HostOs os) {
  switch (os) {
    case HostOs::Windows: return "windows";
    case HostOs::MacOS: return "macos";
    case HostOs::Linux: return "linux";
    case HostOs::Android: return "android";
    default: return "unknown";
  }
}

}  // namespace janus
