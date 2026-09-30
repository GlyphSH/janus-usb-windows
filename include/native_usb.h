#pragma once
#include <stddef.h>
#include <stdint.h>

namespace janus_usb {
bool begin();
size_t available();
int read();
void write_line(const char* line);
bool keyboard_ready();
void key_combo_windows_r();
}
