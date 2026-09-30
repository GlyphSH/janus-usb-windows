# TinyUSB observer integration

`tinyusb_observer.patch` targets upstream TinyUSB `src/device/usbd.c` at the
ESP32-S3 `DCD_EVENT_SETUP_RECEIVED` dispatch point. The Arduino-ESP32 package
ships the matching TinyUSB headers but links a prebuilt device object, so this
patch must be applied in an ESP-IDF/native TinyUSB build configuration before
it can receive real setup packets.
