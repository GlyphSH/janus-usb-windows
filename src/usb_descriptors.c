#include <string.h>
#include "tinyusb.h"
#include "tusb.h"
#include "class/cdc/cdc.h"
#include "class/hid/hid_device.h"

#include "usb_descriptors.h"

/* VID/PID: use TinyUSB's Espressif development pair. See the README:
 * this project makes no unique VID/PID claim for shipping hardware. */
#define JANUS_VID 0x303A
#define JANUS_PID 0x4001
#define JANUS_BCD 0x0100

const tusb_desc_device_t janus_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = JANUS_VID,
    .idProduct          = JANUS_PID,
    .bcdDevice          = JANUS_BCD,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

static const uint8_t janus_hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(),
};

const uint8_t *janus_hid_report_desc(void) {
  return janus_hid_report_descriptor;
}

size_t janus_hid_report_desc_len(void) {
  return sizeof(janus_hid_report_descriptor);
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
  (void)instance;
  return janus_hid_report_descriptor;
}

enum {
  ITF_NUM_CDC = 0,
  ITF_NUM_CDC_DATA,
  ITF_NUM_HID,
  ITF_NUM_TOTAL,
};

#define JANUS_CONFIG_TOTAL_LEN \
  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN)

#define JANUS_EP_CDC_NOTIF 0x81
#define JANUS_EP_CDC_OUT   0x02
#define JANUS_EP_CDC_IN    0x82
#define JANUS_EP_HID_IN    0x83

const uint8_t janus_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, JANUS_CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, JANUS_EP_CDC_NOTIF, 8,
                       JANUS_EP_CDC_OUT, JANUS_EP_CDC_IN, 64),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 5, HID_ITF_PROTOCOL_KEYBOARD,
                       sizeof(janus_hid_report_descriptor),
                       JANUS_EP_HID_IN, 8, 5),
};

const char *janus_string_descriptors[] = {
    (const char[]){0x09, 0x04},
    "GlyphSH",
    "Janus USB",
    "000001",
    "Janus CDC",
    "Janus HID",
};

const size_t janus_string_descriptors_count =
    sizeof(janus_string_descriptors) / sizeof(janus_string_descriptors[0]);
