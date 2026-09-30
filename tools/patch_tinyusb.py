"""PlatformIO pre-action: re-apply the Janus TinyUSB SETUP-observer patch.

The espressif__tinyusb managed component is content-hashed by the ESP-IDF
component manager; edits to it are undone on the next build when the hash is
verified. We patch it in place right before usbd.c is compiled, so the
observer call always ends up in the object file.
"""

import os
import subprocess

Import("env")

PROJECT_DIR = env["PROJECT_DIR"]
PATCH_SCRIPT = os.path.join(PROJECT_DIR, "usb", "apply_tinyusb_observer.sh")
TINYUSB_ROOT = os.path.join(PROJECT_DIR, "managed_components", "espressif__tinyusb")
USBD_TARGET = "$BUILD_DIR/managed_components/espressif__tinyusb/src/device/usbd.c.o"


def _apply(*_, **__):
    if not os.path.isdir(TINYUSB_ROOT):
        return
    subprocess.run(["bash", PATCH_SCRIPT, TINYUSB_ROOT], check=True)


env.AddPreAction(USBD_TARGET, _apply)
