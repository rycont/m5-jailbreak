# M5 post-reboot KWin replay — 2026-10-24

## Result

The user reconnected the smart plug after its temporary network outage; the M5 rebooted. Turnip sent no plug toggle. The direct KWin/Plasma setup was then restored manually from the persistent `/opt/usr/a/m5kde/direct` assets.

KWin reached active Mali OpenGL composition and reported one enabled `LVDS-1` output at `1920x1080`, `60000` mHz. Its log shows 1920x1080 XR24 dma-buf imports and TBM/EGL image creation. KWin and Plasma remained alive, and the presentation probe ran twice after this reboot.

The two post-reboot presentation runs each submitted and presented 600 frames, with no discards, all 600 frame callbacks, and no missed vblanks:

| Run | Elapsed | Presented rate | Refresh interval |
|---|---:|---:|---:|
| retry 4 | 10.233 s | 58.53 fps | 16.666666 ms |
| retry 5 | 10.033 s | 59.70 fps | 16.666666 ms |

Raw outputs are `fps_present_postreboot_retry4.txt` and `fps_present_postreboot_retry5.txt`. The earlier pre-reboot run in the checkpoint was 59.90 fps.

## What the 1080p probe measures

`fps_present_1920` creates a 1920x1080 `wl_shm` surface. The CPU initializes the buffer and paints a moving 128x120 rectangle; each frame damages the rectangle and its previous location. KWin's compositor reports Mali-G51 / OpenGL ES 3.2 and imports its 1920x1080 XR24 buffers through the Tizen TBM/EGL path.

This verifies near-60 Hz presentation of a full-size window through KWin's Mali compositor. The probe does not redraw every pixel on the GPU each frame, so full-screen, full-pixel GPU rendering at 1080p60 remains unmeasured.

## Recovery from the startup failures

1. The first KWin retries faulted at PC `0xe92d4800` with LR offset `0x28628c`; the kernel capture is in `kernel-faults.txt` and `kernel-fault-after-screen-change.txt`. `epoxy_symbol_abi.txt` records that this KWin build imports the four `epoxy_egl*` dispatch slots as ELF `OBJECT` symbols, while the earlier `epoxy_func2_m5.so` exported its functions as `FUNC`. The replacement `epoxy_dispatch_objects.so` exports those slots as `OBJECT`, matching KWin's ABI. With it, KWin passed EGL initialization and reached Mali composition.
2. The first object-shim run still failed output setup: KWin logged `Applying output config failed!`, LVDS was disabled, and the probe got no frame callback. The captured state is in `state_output_disabled_pre_format_fix.txt` and `fps_output_disabled_pre_format_fix.txt`.
3. The dispatch shim was extended to answer dma-buf format and modifier queries for XR24/AR24 with the implicit modifier. KWin then logged successful 1920x1080 XR24 imports and TBM surface/EGL image creation. `kwin_support_after_query_shim.txt` shows `LVDS-1 Enabled: 1`, `Geometry: 0,0,1920x1080`, active composition, and the Mali-G51 renderer.

The symbol-type evidence and successful replay support the ABI diagnosis; the initial kernel fault record remains preserved alongside the successful logs.

## Current visible screen and the Enlightenment question

At 22:00:45 KST the Samsung `samsung.tizen.dcapture.RequestCaptureToFileSync` call returned a 720x540 PNG, `m5_screen_capture_service.png`. The user confirmed that it matches the physical screen: the Tizen HDMI input-wait UI is visible, with a small bright rectangle at the lower left that appears to show the time (approximately `22:00`) and matches the user's description of the floating Plasma dock clock. The low-resolution capture makes the tiny text hard to read precisely.

The capture establishes what was visible; it does not identify which process supplied the HDMI-wait UI pixels or who held DRM Master. Tizen's Enlightenment process runs in another Zone according to the user, so a root-zone `ps` or `/proc` listing cannot prove that E is absent. `m5_root_zone_drm_snapshot.txt` is explicitly limited to the root zone. The pre-reboot conversation recorded E alive with `[SET_MASTER]=0`; that volatile state is not evidence for this post-reboot boot. Current cross-Zone DRM Master ownership is **unverified**. The root-zone debugfs tree has no `/sys/kernel/debug/dri/0/clients` file.

The HDMI prompt and app icons look like Samsung's Tizen source/home UI. Enlightenment may composite that UI, but this capture alone cannot prove that E itself drew the prompt or owns the display master.

## One-second setting-change check

No OpenMausBot automation is registered for this bot. The startup script contains bounded one-second readiness polling while it waits for the Tizen Wayland socket and KWin initialization; it is not a background settings loop. `m5_drm_clockfix.c` changes the mode clock only when KWin calls the DRM connector/CRTC query or set functions. It has no timer or periodic worker. No one-second setting writer was found in the active root-zone process list.

## Replay procedure and persistence boundary

The tested replay was manual after boot:

```sh
sh /opt/usr/a/m5kde/direct/start_clockfix_kde.sh
/opt/usr/a/m5kde/direct/reproduce_1080p60.sh
```

The first script waits for `/run/wayland-0`, restores volatile copies under `/tmp`, starts KWin directly on `card0`, waits for active Mali composition and an enabled LVDS output, then starts Plasma. The second script checks KWin's renderer and runs the 600-frame presentation probe. These scripts are not installed as an automatic boot service.

The replay succeeded on this M5 using its existing persistent prerequisite tree. In addition to the files committed here, that tree contains existing assets under `eglw2/`, `stubs/`, `vgles/`, `kstubs/`, and `kwinwl/`, plus `eglwl_gbm.so` and `noopfd.so`. In particular, `eglw2/libEGL_real.so.1` is a 14,583,825-byte vendor payload. Those pre-existing support payloads were not copied into this commit, so the checkout is not a clean-device installer by itself.

The persistent startup script, patcher, corrected shim, Wayland library, Plasma shim, and frame probe were verified on the M5. `apply_libkwin_patch.py` is included in `scripts/`; it is byte-identical to the deployed patcher. Matching repository/device SHA-256 values are in `persistent_runtime_sha256.txt`.

## Evidence files

- `kwin_output_disabled_pre_format_fix.log`, `kwin_support_output_disabled_pre_format_fix.txt`, `fps_output_disabled_pre_format_fix.txt`, and `state_output_disabled_pre_format_fix.txt` — failed output configuration before the format-query fix.
- `epoxy_symbol_abi.txt` and `kernel-faults.txt` — ELF symbol types and earlier startup fault addresses.
- `kwin_after_query_shim.log`, `kwin_support_after_query_shim.txt`, `clockfix_after_query_shim.log`, `state_after_query_shim.txt` — successful KWin/Mali/output state.
- `fps_present_postreboot_retry4.txt` and `fps_present_postreboot_retry5.txt` — both post-reboot presentation runs.
- `m5_screen_capture_service.png` — user-confirmed Tizen HDMI wait screen and small lower-left clock-like badge.
- `m5_root_zone_drm_snapshot.txt` — current root-zone process/fd and KWin output snapshot; it explicitly does not cover Enlightenment's Zone.
- Earlier retries and failures remain in this directory, including `kernel-fault-after-screen-change.txt`, `kwin_after_screen_change.log`, and `plasma_after_kwin_crash.log`.
