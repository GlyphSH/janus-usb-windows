#!/usr/bin/env bash
set -euo pipefail

# Apply the observer patch to a checked-out TinyUSB tree. This keeps the
# upstream source untouched and makes the native ESP32-S3 integration
# reproducible for contributors.
tinyusb_root=${1:?usage: apply_tinyusb_observer.sh /path/to/tinyusb}
cp "$(dirname "$0")/tinyusb_observer.h" "$tinyusb_root/src/device/janus_usb_observer.h"
python3 - "$tinyusb_root/src/device/usbd.c" <<'PY'
import pathlib, re, sys
p = pathlib.Path(sys.argv[1])
s = p.read_text()
if 'janus_usb_observer.h' not in s:
    s = s.replace('#include "tusb.h"', '#include "tusb.h"\n#include "janus_usb_observer.h"', 1)
needle = '        _usbd_queued_setup--;\n'
call_line = '        janus_usb_observe_setup((uint32_t)esp_timer_get_time(), event.setup_received.bmRequestType, event.setup_received.bRequest, event.setup_received.wValue, event.setup_received.wIndex, event.setup_received.wLength);\n'
# Drop any prior observer call so we can re-apply the current form idempotently.
s = re.sub(r'^ *janus_usb_observe_setup\([^;]*\);\n', '', s, flags=re.MULTILINE)
if needle not in s: raise SystemExit('setup dispatch anchor not found')
s = s.replace(needle, needle + call_line, 1)
p.write_text(s)
PY
echo "TinyUSB observer applied to $tinyusb_root"
