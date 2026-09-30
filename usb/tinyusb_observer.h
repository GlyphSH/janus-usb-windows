#pragma once

/* TinyUSB integration seam. The vendored usbd.c calls this for every SETUP
 * packet before dispatching it to the class driver. Keep this header free of
 * Arduino dependencies so the same observer can be unit tested on a host. */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward-declared to avoid adding esp_timer as an include dep on the vendored
 * tinyusb component. Returns microseconds since boot. */
int64_t esp_timer_get_time(void);

void janus_usb_observe_setup(uint32_t elapsed_us, uint8_t bm_request_type,
                             uint8_t b_request, uint16_t w_value,
                             uint16_t w_index, uint16_t w_length);

#ifdef __cplusplus
}
#endif
