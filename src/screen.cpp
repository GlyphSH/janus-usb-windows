#include "screen.h"

#include <cstdio>
#include <cstring>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "lcd/esp_lcd_st7735.h"
#include "lcd/font8x8_basic.h"

namespace {
constexpr const char *TAG = "screen";

constexpr int PIN_MOSI = 3;
constexpr int PIN_SCLK = 5;
constexpr int PIN_CS = 4;
constexpr int PIN_DC = 2;
constexpr int PIN_RST = 1;
constexpr int PIN_BL = 38;
constexpr spi_host_device_t LCD_HOST = SPI2_HOST;

constexpr int WIDTH = 160;
constexpr int HEIGHT = 80;

// Y-only 2x scale -> 8x16 per char -> 20 cols x 5 rows.
constexpr int SCALE_X = 1;
constexpr int SCALE_Y = 2;
constexpr int CHAR_W = 8 * SCALE_X;
constexpr int CHAR_H = 8 * SCALE_Y;
constexpr int COLS = WIDTH / CHAR_W;   // 20
constexpr int ROWS = HEIGHT / CHAR_H;  // 5

esp_lcd_panel_handle_t g_panel = nullptr;
esp_lcd_panel_io_handle_t g_io = nullptr;
uint16_t *g_fb = nullptr;
SemaphoreHandle_t g_mutex = nullptr;

// RGB565 helpers. Panel is BGR-ordered with byte swap; we store big-endian.
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  const uint16_t v =
      static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  return static_cast<uint16_t>((v >> 8) | (v << 8));
}

constexpr uint16_t COLOR_BG = rgb(0, 0, 0);
constexpr uint16_t COLOR_FG = rgb(0, 255, 128);
constexpr uint16_t COLOR_DIM = rgb(0, 128, 64);
constexpr uint16_t COLOR_HI = rgb(255, 255, 0);
constexpr uint16_t COLOR_ERR = rgb(255, 64, 32);

void fb_clear(uint16_t color) {
  for (int i = 0; i < WIDTH * HEIGHT; ++i) g_fb[i] = color;
}

void draw_char_at(int col, int row, char c, uint16_t color) {
  if (col < 0 || col >= COLS || row < 0 || row >= ROWS) return;
  const uint8_t ch = static_cast<uint8_t>(c);
  const unsigned char *glyph = font8x8_basic[ch & 0x7F];
  const int x0 = col * CHAR_W;
  const int y0 = row * CHAR_H;
  for (int gy = 0; gy < 8; ++gy) {
    const uint8_t row_bits = glyph[gy];
    for (int gx = 0; gx < 8; ++gx) {
      if (!(row_bits & (1u << gx))) continue;
      const int px = x0 + gx * SCALE_X;
      const int py = y0 + gy * SCALE_Y;
      for (int dy = 0; dy < SCALE_Y; ++dy) {
        for (int dx = 0; dx < SCALE_X; ++dx) {
          const int i = (py + dy) * WIDTH + (px + dx);
          if (i >= 0 && i < WIDTH * HEIGHT) g_fb[i] = color;
        }
      }
    }
  }
}

void draw_text(int col, int row, const char *s, uint16_t color) {
  while (*s && col < COLS) {
    draw_char_at(col, row, *s, color);
    ++s;
    ++col;
  }
}

void backlight_on() {
  ledc_timer_config_t timer_cfg = {};
  timer_cfg.duty_resolution = LEDC_TIMER_8_BIT;
  timer_cfg.freq_hz = 1000;
  timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
  timer_cfg.timer_num = LEDC_TIMER_0;
  timer_cfg.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&timer_cfg);

  ledc_channel_config_t ch_cfg = {};
  ch_cfg.channel = LEDC_CHANNEL_3;
  ch_cfg.duty = 32;  // ~12% (backlight is active LOW so this is bright)
  ch_cfg.gpio_num = PIN_BL;
  ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
  ch_cfg.timer_sel = LEDC_TIMER_0;
  ledc_channel_config(&ch_cfg);
}

}  // namespace

namespace janus::screen {

bool begin() {
  g_mutex = xSemaphoreCreateMutex();
  g_fb = static_cast<uint16_t *>(heap_caps_malloc(
      WIDTH * HEIGHT * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (!g_fb) {
    ESP_LOGE(TAG, "framebuffer alloc failed");
    return false;
  }

  spi_bus_config_t bus = ST7735_PANEL_BUS_SPI_CONFIG(PIN_SCLK, PIN_MOSI,
                                                     WIDTH * HEIGHT * sizeof(uint16_t));
  esp_err_t err = spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "spi_bus_initialize: %d", err);
    return false;
  }

  esp_lcd_panel_io_spi_config_t io_cfg =
      ST7735_PANEL_IO_SPI_CONFIG(PIN_CS, PIN_DC, NULL, NULL);
  err = esp_lcd_new_panel_io_spi(
      (esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &g_io);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "panel_io_spi: %d", err);
    return false;
  }

  esp_lcd_panel_dev_config_t panel_cfg = {};
  panel_cfg.reset_gpio_num = PIN_RST;
  panel_cfg.color_space = ESP_LCD_COLOR_SPACE_BGR;
  panel_cfg.bits_per_pixel = 16;
  err = esp_lcd_new_panel_st7735(g_io, &panel_cfg, &g_panel);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "new_panel_st7735: %d", err);
    return false;
  }
  esp_lcd_panel_reset(g_panel);
  esp_lcd_panel_init(g_panel);
  esp_lcd_panel_invert_color(g_panel, true);
  esp_lcd_panel_set_gap(g_panel, 1, 26);
  esp_lcd_panel_swap_xy(g_panel, true);
  esp_lcd_panel_mirror(g_panel, false, true);
  esp_lcd_panel_disp_on_off(g_panel, true);

  backlight_on();

  fb_clear(COLOR_BG);
  draw_text(0, 0, "JANUS USB", COLOR_HI);
  draw_text(0, 1, "boot...", COLOR_DIM);
  esp_lcd_panel_draw_bitmap(g_panel, 0, 0, WIDTH, HEIGHT, g_fb);
  ESP_LOGI(TAG, "screen ready");
  return true;
}

void update(const ScreenState &s) {
  if (!g_panel) return;
  if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;

  fb_clear(COLOR_BG);

  char line[40];

  // Row 0: title
  draw_text(0, 0, "JANUS USB", COLOR_HI);

  // Row 1: USB / HID status
  std::snprintf(line, sizeof(line), "USB:%s HID:%s CDC:%s",
                s.usb_mounted ? "up" : "dn",
                s.hid_ready ? "up" : "dn",
                s.cdc_connected ? "up" : "dn");
  draw_text(0, 1, line, s.usb_mounted ? COLOR_FG : COLOR_ERR);

  // Row 2: last CDC command
  std::snprintf(line, sizeof(line), "cmd: %.14s",
                (s.last_cdc_line && s.last_cdc_line[0]) ? s.last_cdc_line : "-");
  draw_text(0, 2, line, s.cdc_connected ? COLOR_FG : COLOR_DIM);

  // Row 3: fingerprint OS + confidence
  std::snprintf(line, sizeof(line), "FP: %-8s %u%%",
                s.fp_os_name ? s.fp_os_name : "?",
                static_cast<unsigned>(s.fp_confidence));
  const uint16_t fp_c = s.fp_confidence >= 45 ? COLOR_HI : COLOR_FG;
  draw_text(0, 3, line, fp_c);

  // Row 4: SETUP pkt count + LED flag + shortcut count (short labels
  // so 3-digit values still fit in 20 cols).
  std::snprintf(line, sizeof(line), "P:%u L:%u W:%u",
                static_cast<unsigned>(s.fp_request_count),
                static_cast<unsigned>(s.fp_saw_hid_led ? 1 : 0),
                static_cast<unsigned>(s.hid_shortcuts));
  draw_text(0, 4, line, COLOR_DIM);

  esp_lcd_panel_draw_bitmap(g_panel, 0, 0, WIDTH, HEIGHT, g_fb);
  xSemaphoreGive(g_mutex);
}

}  // namespace janus::screen
