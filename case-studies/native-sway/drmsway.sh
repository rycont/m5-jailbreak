#!/bin/sh
# Native Sway on the Samsung M5 panel (ndp DRM, kernel 4.1, no atomic).
export XDG_RUNTIME_DIR=/opt/usr/a/trixie/run
export WLR_BACKENDS=drm
export WLR_DRM_DEVICES=/dev/dri/card0
export WLR_RENDERER=pixman
export WLR_DRM_NO_ATOMIC=1
export WLR_LIBINPUT_NO_DEVICES=1
export WLR_NO_HARDWARE_CURSORS=1
export LIBSEAT_BACKEND=builtin
export LD_PRELOAD=/opt/usr/a/libdrmcap.so
export LD_LIBRARY_PATH=/opt/usr/a/trixie/usr/lib/arm-linux-gnueabihf:/opt/usr/a/trixie/lib/arm-linux-gnueabihf:/opt/usr/a/trixie/usr/lib:/opt/usr/a/trixie/lib
export PATH=/opt/usr/a/trixie/usr/bin:/usr/bin:/bin
export XDG_DATA_DIRS=/opt/usr/a/trixie/usr/share:/usr/share
export TERMINFO=/usr/share/terminfo
exec /opt/usr/a/trixie/usr/bin/sway -c /tmp/m5drm.config
