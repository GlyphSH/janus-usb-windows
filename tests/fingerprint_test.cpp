#include <cassert>
#include <cstring>
#include "usb_fingerprint.h"

using janus::HostOs;
using janus::UsbFingerprint;

int main() {
  UsbFingerprint windows;
  windows.observe_setup({0, 0x80, 0x06, 0x0100, 0, 18});
  windows.observe_setup({1000, 0x80, 0x06, 0x0200, 0, 9});
  windows.observe_setup({2000, 0x80, 0x06, 0x2200, 0, 64});
  windows.observe_hid_led(0, 5000);
  auto result = windows.classify();
  assert(result.os == HostOs::Windows);
  assert(result.saw_hid_led_report);
  assert(result.confidence >= 45);
  assert(std::strcmp(janus::host_os_name(result.os), "windows") == 0);

  UsbFingerprint unknown;
  unknown.observe_setup({0, 0x80, 0x06, 0x0100, 0, 18});
  assert(unknown.classify().os == HostOs::Unknown);
  unknown.reset();
  assert(unknown.classify().request_count == 0);
}
