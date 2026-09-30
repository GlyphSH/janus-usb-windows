# Janus USB for Windows

**Status: prototype.** The Arduino firmware builds and its serial protocol is
tested. The native ESP32-S3 TinyUSB migration is still in progress and has not
been flashed or validated on a physical host.

Fresh LilyGo T-Dongle S3 firmware: USB CDC serial text-file transfers and a
physical-button HID shortcut to open Windows CMD. MIT licensed. This first
version stores one file of up to 64 KiB in RAM; unplugging or resetting clears
it. Bytes are preserved, including Unicode text and Windows line endings.

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
`roundtrip`. CRC detects corruption, not malicious modification. On a timeout,
reconnect and restart the transfer. An incomplete upload does not replace the
previous committed file. Only one host tool should use the port at a time.

## Keyboard demo

With an unlocked Windows desktop and English/US keyboard layout, hold BOOT for
1-5 seconds and release. It types Win+R, `cmd`, Enter. No administrator elevation
or additional commands. There is no automatic typing at insertion. Window-focus
and keyboard-layout behavior need physical Windows testing.

## Tests

```sh
c++ -std=c++17 -Wall -Wextra -Werror -I include src/transfer.cpp tests/transfer_test.cpp -o /tmp/janus-test
/tmp/janus-test
python -m unittest discover -s tests
```

Hardware enumeration, button timing, and real Windows transfer tests remain
required before a release. Framework development USB identifiers are used;
this project does not claim a unique VID/PID allocation for shipping hardware.

The USB fingerprinting layer is trace-driven. `usb_fingerprint.*` records
standard setup packets and HID LED reports and classifies only signals it can
justify; unknown traces remain `unknown`. The Arduino USB wrapper does not
expose every standard setup packet yet, so the native TinyUSB observer is a
separate integration step. This boundary prevents the firmware from claiming
passive OS detection before it has actually observed the enumeration stream.

Vendor references: [LILYGO board](https://github.com/Xinyuan-LilyGO/T-Dongle-S3),
[Arduino ESP32 USB implementation](https://github.com/espressif/arduino-esp32/tree/2.0.17/libraries/USB).

## Windows interface model

Janus exposes CDC-ACM and HID as separate USB interfaces in one composite
device. Windows binds the CDC child through `usbser.sys` and presents the
serial endpoint as `COM<N>`; it binds the HID child through the standard HID
stack. The native TinyUSB descriptor in `usb/usb_descriptors_native.c` keeps
those interfaces separate so Windows PnP can create the expected child
devices.
