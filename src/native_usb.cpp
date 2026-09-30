#include "native_usb.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {
#include "tinyusb.h"
#include "tusb.h"
#include "class/cdc/cdc_device.h"
#include "class/hid/hid_device.h"
#include "usb_descriptors.h"
}

namespace {
constexpr const char *TAG = "janus_usb";
}

namespace janus_usb {

bool begin() {
  tinyusb_config_t cfg = {};
  cfg.device_descriptor = &janus_device_descriptor;
  cfg.string_descriptor = janus_string_descriptors;
  cfg.string_descriptor_count = static_cast<int>(janus_string_descriptors_count);
  cfg.external_phy = false;
  cfg.configuration_descriptor = janus_configuration_descriptor;

  esp_err_t err = tinyusb_driver_install(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "tinyusb_driver_install failed: %d", err);
    return false;
  }
  ESP_LOGI(TAG, "TinyUSB installed (CDC+HID composite)");
  return true;
}

size_t available() { return tud_cdc_n_available(0); }

int read() {
  uint8_t c = 0;
  return tud_cdc_n_read(0, &c, 1) == 1 ? static_cast<int>(c) : -1;
}

void write_line(const char *line) {
  if (!tud_cdc_n_connected(0)) return;
  tud_cdc_n_write_str(0, line);
  tud_cdc_n_write_char(0, '\r');
  tud_cdc_n_write_char(0, '\n');
  tud_cdc_n_write_flush(0);
}

bool keyboard_ready() { return tud_hid_ready(); }

void key_combo_windows_r() {
  if (!tud_hid_ready()) return;

  // GUI + R
  uint8_t keys[6] = {HID_KEY_R, 0, 0, 0, 0, 0};
  tud_hid_keyboard_report(0, KEYBOARD_MODIFIER_LEFTGUI, keys);
  vTaskDelay(pdMS_TO_TICKS(30));
  tud_hid_keyboard_report(0, 0, nullptr);
  vTaskDelay(pdMS_TO_TICKS(120));

  // Type "cmd"
  auto tap = [](uint8_t k) {
    uint8_t k6[6] = {k, 0, 0, 0, 0, 0};
    tud_hid_keyboard_report(0, 0, k6);
    vTaskDelay(pdMS_TO_TICKS(20));
    tud_hid_keyboard_report(0, 0, nullptr);
    vTaskDelay(pdMS_TO_TICKS(20));
  };
  tap(HID_KEY_C);
  tap(HID_KEY_M);
  tap(HID_KEY_D);
  vTaskDelay(pdMS_TO_TICKS(40));
  tap(HID_KEY_RETURN);
}

}  // namespace janus_usb

extern "C" {

// HID host->device: we don't accept GET_REPORT queries; return zero length.
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
  (void)instance;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)reqlen;
  return 0;
}

// HID host->device: LED report from Windows lands here; forward to fingerprint.
extern void janus_usb_observe_hid_led(uint8_t led_mask);

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {
  (void)instance;
  (void)report_id;
  if (report_type == HID_REPORT_TYPE_OUTPUT && bufsize >= 1) {
    janus_usb_observe_hid_led(buffer[0]);
  }
}

}  // extern "C"
