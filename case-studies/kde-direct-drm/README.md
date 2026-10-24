# KDE direct DRM on the M5 (2026-10-24)

This is the first **relay-free** KDE Plasma path on the M5.

- KWin 6.3.6 talks to `/dev/dri/card0` directly.
- Plasma 6.3.6 connects to the KWin Wayland socket.
- No relay process, no nested compositor, no QPainter in the output path.
- Evidence: `evidence/m5-kwin-direct-2026-10-24.png` (green desktop + bottom panel).
- KWin and plasmashell stayed alive together for several minutes (`kwin=3091` at 5:36,
  `plasma=538` at 0:45 when the capture was taken).

## What was blocking KWin

The existing KWin binary is Debian armel 6.3.6. Two separate crash sites appeared
when it was run with the real DRM backend:

1. `KWin::PointerSwipeGestureV1Interface::sendCancel()` — LR mapped to
   `pointergestures_v1.cpp:146` / `qmap.h:1120`, but the faulting PC was the
   invalid address `0xe92d4800`.
2. After bypassing that, `KWin::AbstractEglBackend::initClientExtensions()` at
   `abstract_egl_backend.cpp:199`; the faulting PC was again `0xe92d4800`.
   The source line is the `eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS)` /
   `qstrlen()` path. The exact reason the return is bad is not fully proven yet;
   this is a workaround.

The debug image used for the mapping was fetched from Debian debuginfod by
Build ID `ba4770b6b33dfbf42b13a5fc21dd75eefe44a377` (71 MB,
`libkwin.debug`). The offsets below are file/VMA offsets in that exact binary.

## Patches

Both functions are changed to an immediate `bx lr`:

| Offset | Function | Original | Patched |
|---|---|---|---|
| `0x58d6bc` | `PointerSwipeGestureV1Interface::sendCancel(unsigned int)` | `01 ca a0 e3` (`mov r12, #4096`) | `1e ff 2f e1` (`bx lr`) |
| `0x50d77c` | `AbstractEglBackend::initClientExtensions()` | `01 ca a0 e3` | `1e ff 2f e1` |

Apply with:

```sh
python3 scripts/apply_libkwin_patch.py /opt/usr/a/lib/libkwin.so.6
```

The script creates `/opt/usr/a/lib/libkwin.so.6.bak-direct-20261024` and is
idempotent. It refuses to patch if the original bytes or file size do not match.

## Start

After root SSH is restored on 2222:

```sh
# once after boot, if /tmp was wiped:
#   /opt/usr/a/m5kde/direct contains the shims (44 MB)
python3 /opt/usr/a/m5kde/direct/apply_libkwin_patch.py /opt/usr/a/lib/libkwin.so.6
sh /opt/usr/a/m5kde/direct/start_direct_kde.sh
```

The start script does the following, in order:

1. restores `/tmp/stubs`, `/tmp/eglw2`, `/tmp/vgles`, `/tmp/kstubs`, `/tmp/kwinwl`
   from `/opt/usr/a/m5kde/direct`;
2. starts the Tizen `/usr/bin/dbus-daemon` with `env -u LD_LIBRARY_PATH`
   (otherwise the armel `libdbus` poisons the Tizen binary);
3. runs `kwin_wayland --drm --socket wayland-4` with:

   ```text
   LD_LIBRARY_PATH=/opt/usr/a/lib:/tmp/kwinwl:/tmp/vgles:/tmp/eglw2:/tmp/kstubs:...
   LD_PRELOAD=/opt/usr/a/m5kde/direct/epoxy_func2.so:
             .../eglwl_gbm.so:/tmp/eglw2/libEGL.so.1:
             .../gbm_cache.so:/tmp/stubs/libgbmshim2.so:
             .../noopfd.so:/tmp/stubs/libwlservshim.so:/tmp/stubs/libtbmsym.so
   KWIN_COMPOSE=O2ES KWIN_DRM_DEVICES=/dev/dri/card0 KWIN_DRM_NO_AMS=1
   ```

4. starts `kactivitymanagerd` and `plasmashell` with the **unmodified**
   `LP_PLASMA=$A/lib:$A/usr/lib/arm-linux-gnueabi:$A/usr/lib:/usr/lib` and
   `QT_QUICK_BACKEND=software`, on `WAYLAND_DISPLAY=wayland-4`.

## Evidence

- `evidence/m5-kwin-direct-2026-10-24.png` — Tizen `dcapture` (720x540 fixed):
  green desktop and the bottom Plasma panel; right side has a dark strip.
- `notes/REPORT_2026-10-24_1027.md` — exact PIDs, DBus names, logs, and
  before/after crash mapping.
- On the device:
  - `/tmp/kwin_direct.log`
  - `/tmp/plasma_direct.log`
  - `/tmp/dbus_direct.log`
  - `/opt/usr/a/lib/libkwin.so.6.bak-direct-20261024`

## Known gaps

- The panel content/clock is not visible in this capture; the panel strip is
  blank. The right edge is a dark strip, not yet explained.
- 1920x1080@60 has **not** been measured on this direct path.
- The two `bx lr` patches are crash bypasses. A clean fix needs a correct
  `eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS)` result and a real solution
  for the `sendCancel` path.
- `/tmp` must be repopulated after each boot; the script and the 44 MB
  `/opt/usr/a/m5kde/direct` snapshot exist for that.
- Root SSH itself still needs `restore_after_powercycle.sh` after a power cycle
  because UEP/dropbear live in RAM.
