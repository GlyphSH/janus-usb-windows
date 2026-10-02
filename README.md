# Janus USB for Windows

**Status: prototype.** The firmware builds and its serial protocol, USB
fingerprint classifier, and PowerShell / Python / C++ CRC32 implementations
are tested in CI, including ASan+UBSan on the C++ handler and a cross-
implementation CRC equivalence check against `zlib.crc32`. The native
ESP32-S3 TinyUSB build has been flashed to a LilyGo T-Dongle S3 and
file roundtrips verified against a physical Windows host by the author;
hardware enumeration and button-timing tests are not yet in CI.

Fresh LilyGo T-Dongle S3 firmware: USB CDC serial text-file transfers and a
physical-button HID shortcut to open Windows CMD. MIT licensed. Files up to
64 KiB are staged in RAM and, when an SD card is inserted, also mirrored to
`/sd/janus-received.bin` on commit. Bytes are preserved, including Unicode
text and Windows line endings.

## Build

Install Python 3, then in this directory:

```powershell
py -m pip install -r requirements.txt
py -m platformio run
```

Hold BOOT while inserting the dongle to enter download mode. Find its download
port, then explicitly flash (this replaces the installed firmware):

```powershell
py -m platformio run --target upload --upload-port COM7
```

Unplug and reinsert normally. Windows may assign a different port to the new
composite firmware. COM1 is the tool's default, not a firmware-assigned port:

```powershell
py tools\janus.py ports
py tools\janus.py --port COM7 roundtrip input.txt returned.txt
py tools\janus.py --port COM7 put input.txt
py tools\janus.py --port COM7 get downloaded.txt
```

A pure-PowerShell port with the same wire protocol is at `tools/janus.ps1`,
with `tools/janus.bat` and `tools/janus.vbs` as thin launchers. Any of the
four is fine; pick by dependency and shell:

```powershell
.\tools\janus.ps1 -Port COM7 put input.txt      # no Python required
tools\janus.bat -Port COM7 put input.txt        # cmd or right-click
wscript tools\janus.vbs -Port COM7 put input.txt  # hidden, result in a message box
```

Output files must not already exist. Close other serial terminals first.
Transfers use stop-and-wait chunks, CRC32, and exact byte comparison for
`roundtrip`. CRC detects corruption, not malicious modification. The host
clients retry a failed COMMIT or a verification CRC mismatch up to three
times. On a timeout, reconnect and restart the transfer. An incomplete
upload does not replace the previous committed file; neither does a torn
SD write (the mirror lands via write-to-tmp-then-rename). Only one host
tool should use the port at a time.
After `put`/`roundtrip` the client also queries `SDINFO` and prints whether
the dongle wrote the file to its SD card.

## SD demo

`tools/demo-sd.bat` is a one-double-click demo for showing the transfer to
colleagues. It reads `C:\test.txt` (creating it with a timestamped line if
absent), transfers it to the dongle, and prints the SD-write result. Pass a
COM port as the first argument, or edit the default at the top of
`tools/demo-sd.ps1`:

```powershell
tools\demo-sd.bat COM7
```

Insert an SD card in the dongle before running. Without one, the transfer
still succeeds and the client reports `SD not written`.

## Keyboard demo

With an unlocked Windows desktop and English/US keyboard layout, hold BOOT for
1-5 seconds and release. It types Win+R, `cmd`, Enter. No administrator elevation
or additional commands. There is no automatic typing at insertion. Window-focus
and keyboard-layout behavior need physical Windows testing.

## Tests

```sh
# Protocol handler, ABORT / SDDIAG / re-BEGIN state machine, CRC vector corpus.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I include src/transfer.cpp tests/transfer_test.cpp -o /tmp/janus-test
/tmp/janus-test

# USB fingerprint classifier (host build).
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I include src/usb_fingerprint.cpp tests/fingerprint_test.cpp -o /tmp/janus-fp-test
/tmp/janus-fp-test

# Python client unit tests plus CRC32 reference corpus.
python -m unittest discover -s tests

# Cross-implementation CRC32 agreement (cpp vs zlib). The PowerShell
# implementation is pinned against the same corpus in the Windows CI job.
python tests/crc_cross_check.py
```

Hardware enumeration, button timing, and real Windows transfer tests remain
required before a release. Framework development USB identifiers are used;
this project does not claim a unique VID/PID allocation for shipping hardware.

## USB fingerprint classifier

`usb_fingerprint.*` records standard setup packets and HID LED reports and
classifies only signals it can justify; unknown traces remain `unknown`.
The native TinyUSB observer in `usb/tinyusb_observer.patch` feeds it 64-bit
`esp_timer_get_time()` microseconds, so an idle-then-replugged device does
not hit the ~71-minute `uint32_t` wrap. The `FINGERPRINT` CDC verb returns
a one-line summary of what *this host* sent during its own enumeration.

Threat model: the verb is reachable only to a host that already owns the
CDC endpoint, i.e. physical USB access. It cannot leak traces from another
host, and the classifier is passive: it never scans, probes, or persists
anything beyond RAM. This device is a bench tool, not a defensive endpoint;
if that profile changes, gate the verb behind a build flag.

Vendor references: [LILYGO board](https://github.com/Xinyuan-LilyGO/T-Dongle-S3),
[Arduino ESP32 USB implementation](https://github.com/espressif/arduino-esp32/tree/2.0.17/libraries/USB).

## Windows interface model

Janus exposes CDC-ACM and HID as separate USB interfaces in one composite
device. Windows binds the CDC child through `usbser.sys` and presents the
serial endpoint as `COM<N>`; it binds the HID child through the standard HID
stack. The native TinyUSB descriptor in `usb/usb_descriptors_native.c` keeps
those interfaces separate so Windows PnP can create the expected child
devices.
