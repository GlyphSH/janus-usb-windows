#include <cstdint>
#include <cstdio>
#include <string>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "event_log.h"
#include "native_usb.h"
#include "screen.h"
#include "transfer.h"
#include "usb_fingerprint.h"

extern "C" {
#include "tusb.h"
}

namespace {
constexpr const char *TAG = "janus";
constexpr gpio_num_t BUTTON_PIN = GPIO_NUM_0;

// Design note (alternatives considered):
//   g_transfer is a namespace-scope global so its 128 KiB of buffers live
//   in BSS, not on the FreeRTOS task stack. The alternative is to allocate
//   on the heap at app_main entry (via std::unique_ptr<Transfer>), which
//   would survive a hypothetical module relocation and could be shared
//   between tasks. We picked BSS because this firmware has exactly one
//   serial dispatch task and one Transfer instance for the device's
//   lifetime; the global makes the single-ownership invariant visible at
//   file scope and avoids any heap-exhaustion failure mode during boot.
Transfer g_transfer;
EventLog g_event_log;
janus::UsbFingerprint g_fingerprint;
std::string g_line;
bool g_discard = false;
std::string g_last_cdc_line;
uint32_t g_hid_shortcuts = 0;

uint32_t now_ms() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

void open_cmd() {
  if (!janus_usb::keyboard_ready()) return;
  janus_usb::key_combo_windows_r();
  ++g_hid_shortcuts;
  g_event_log.record("HID_CMD_OPENED");
}

void poll_button() {
  static bool armed = false;
  static bool pressed = false;
  static uint32_t started = 0;
  const bool down = gpio_get_level(BUTTON_PIN) == 0;
  if (!down && !pressed) armed = true;
  if (down && armed && !pressed) {
    pressed = true;
    started = now_ms();
  }
  if (!down && pressed) {
    const uint32_t held = now_ms() - started;
    pressed = false;
    armed = false;
    if (held >= 1000 && held <= 5000) open_cmd();
  }
}

// Dispatcher budget: at most 1024 bytes drained per 2 ms tick (see the
// vTaskDelay in app_main). At 115200 baud that's ~230 bytes/tick worst
// case, so the budget comfortably outpaces the wire; the explicit cap
// keeps a stuck/flooding host from monopolising the task.
void poll_serial() {
  for (int budget = 0; budget < 1024 && janus_usb::available(); ++budget) {
    const int b = janus_usb::read();
    if (b < 0) break;
    const char c = static_cast<char>(b);
    if (c == '\r') continue;
    if (c == '\n') {
      std::string reply;
      if (g_discard) {
        reply = "ERR LINE";
      } else if (g_line == "CLIENT windows") {
        g_event_log.record("CLIENT_WINDOWS");
        reply = "OK CLIENT";
      } else if (g_line == "FINGERPRINT") {
        // Threat model: this verb is reachable only to a host that already
        // owns the CDC endpoint (i.e. physical USB access). The reply is a
        // conservative classification of USB SETUP packets that *this host*
        // sent during enumeration; it cannot leak traces from another host.
        // See README.md "USB fingerprint classifier" for the full rationale.
        const auto r = g_fingerprint.classify();
        char buf[64];
        std::snprintf(buf, sizeof(buf), "FP %s %u %u %u",
                      janus::host_os_name(r.os),
                      static_cast<unsigned>(r.confidence),
                      static_cast<unsigned>(r.saw_hid_led_report ? 1 : 0),
                      static_cast<unsigned>(r.request_count));
        reply = buf;
      } else {
        reply = g_transfer.command(g_line);
      }
      const bool successful = reply.rfind("OK", 0) == 0 || reply.rfind("JANUS", 0) == 0;
      g_event_log.record(successful ? "SERIAL_OK" : "SERIAL_ERROR", g_line.c_str());
      janus_usb::write_line(reply.c_str());
      g_last_cdc_line = g_line;
      g_line.clear();
      g_discard = false;
    } else if (!g_discard) {
      if (g_line.size() >= Transfer::max_data_line) {
        g_line.clear();
        g_discard = true;
      } else {
        g_line += c;
      }
    }
  }
}

void configure_button() {
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << BUTTON_PIN;
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&cfg);
}

}  // namespace

extern "C" void janus_usb_observe_setup(uint64_t elapsed_us,
                                        uint8_t bm_request_type, uint8_t b_request,
                                        uint16_t w_value, uint16_t w_index,
                                        uint16_t w_length) {
  g_fingerprint.observe_setup(
      {elapsed_us, bm_request_type, b_request, w_value, w_index, w_length});
}

extern "C" void janus_usb_observe_hid_led(uint8_t led_mask) {
  g_fingerprint.observe_hid_led(led_mask,
                                static_cast<uint64_t>(esp_timer_get_time()));
}

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "boot");

  esp_err_t nvs = nvs_flash_init();
  if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
  }

  configure_button();
  g_line.reserve(Transfer::max_data_line);
  g_event_log.begin();  // best-effort: no SD is fine
  janus::screen::begin();

  // Give a serial-JTAG console client 3s to attach and capture boot log
  // before TinyUSB grabs the USB PHY.
  for (int i = 3; i > 0; --i) {
    ESP_LOGI(TAG, "starting TinyUSB in %ds...", i);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  if (!janus_usb::begin()) {
    ESP_LOGE(TAG, "USB init failed; halting");
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
  }
  ESP_LOGI(TAG, "TinyUSB up");

  xTaskCreate(
      [](void *) {
        while (true) {
          const auto fp = g_fingerprint.classify();
          janus::ScreenState s = {};
          s.usb_mounted = tud_mounted();
          s.cdc_connected = tud_cdc_n_connected(0);
          s.hid_ready = tud_hid_ready();
          s.last_cdc_line = g_last_cdc_line.c_str();
          s.fp_os_name = janus::host_os_name(fp.os);
          s.fp_confidence = fp.confidence;
          s.fp_saw_hid_led = fp.saw_hid_led_report;
          s.fp_request_count = fp.request_count;
          s.hid_shortcuts = g_hid_shortcuts;
          janus::screen::update(s);
          vTaskDelay(pdMS_TO_TICKS(200));
        }
      },
      "screen", 4096, nullptr, 3, nullptr);

  ESP_LOGI(TAG, "ready");
  while (true) {
    poll_serial();
    poll_button();
    g_event_log.tick();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}
