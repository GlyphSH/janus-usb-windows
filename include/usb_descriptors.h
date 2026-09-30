#pragma once
#include <stddef.h>
#include <stdint.h>

#include "tusb.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const tusb_desc_device_t janus_device_descriptor;
extern const uint8_t janus_configuration_descriptor[];
extern const char *janus_string_descriptors[];
extern const size_t janus_string_descriptors_count;

const uint8_t *janus_hid_report_desc(void);
size_t janus_hid_report_desc_len(void);

#ifdef __cplusplus
}
#endif
