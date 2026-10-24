#!/usr/bin/env python3
"""Patch KWin 6.3.6 to bypass two crash paths seen on the M5.

Target: /opt/usr/a/lib/libkwin.so.6 (Debian armel, KWin 6.3.6)
Build ID used for the debug image: ba4770b6b33dfbf42b13a5fc21dd75eefe44a377

The two functions are changed to `bx lr` (ARM little-endian 1e ff 2f e1):

  0x58d6bc  KWin::PointerSwipeGestureV1Interface::sendCancel(unsigned int)
  0x50d77c  KWin::AbstractEglBackend::initClientExtensions()

This is a workaround, not an upstream fix. Keep the backup.
"""
import hashlib
import os
import shutil
import sys

TARGET = sys.argv[1] if len(sys.argv) > 1 else "/opt/usr/a/lib/libkwin.so.6"
BX_LR = bytes.fromhex("1e ff 2f e1")
PATCHES = {
    0x58D6BC: bytes.fromhex("01 ca a0 e3"),  # mov r12, #4096
    0x50D77C: bytes.fromhex("01 ca a0 e3"),  # mov r12, #4096
}
BACKUP = TARGET + ".bak-direct-20261024"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    if not os.path.isfile(TARGET):
        raise SystemExit(f"missing: {TARGET}")
    data = bytearray(open(TARGET, "rb").read())
    if len(data) <= max(PATCHES) + 4:
        raise SystemExit("unexpected file size")

    print("target:", TARGET)
    print("before:", sha256(TARGET), len(data), "bytes")

    changed = False
    for off, old in PATCHES.items():
        cur = bytes(data[off:off + 4])
        if cur == BX_LR:
            print(f"0x{off:x}: already patched")
            continue
        if cur != old:
            raise SystemExit(
                f"0x{off:x}: unexpected bytes {cur.hex()} "
                f"(want {old.hex()}); refusing to patch"
            )
        if not changed and not os.path.exists(BACKUP):
            shutil.copy2(TARGET, BACKUP)
            print("backup:", BACKUP)
        data[off:off + 4] = BX_LR
        print(f"0x{off:x}: {old.hex()} -> {BX_LR.hex()}")
        changed = True

    if changed:
        with open(TARGET, "wb") as f:
            f.write(data)
        print("after: ", sha256(TARGET))
    else:
        print("nothing to do")


if __name__ == "__main__":
    main()
