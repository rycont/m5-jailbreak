#!/bin/sh
set -eu

A=/opt/usr/a/armel
D=/opt/usr/a/m5kde/direct
SOCK=/run/wayland-4
DBUS=unix:path=/tmp/dbus-kde
LP=$D:$A/lib:$A/usr/lib/arm-linux-gnueabi:$A/usr/lib:/usr/lib

if [ ! -S "$SOCK" ]; then
    echo "ERROR: direct KWin socket is not up: $SOCK" >&2
    exit 1
fi
if ! DBUS_SESSION_BUS_ADDRESS="$DBUS" dbus-send --session \
    --dest=org.kde.KWin --type=method_call --print-reply \
    /KWin org.kde.KWin.supportInformation 2>/dev/null | \
    grep -q 'OpenGL renderer string: Mali-G51'; then
    echo "ERROR: direct KWin is not reporting active Mali OpenGL" >&2
    exit 1
fi
if [ ! -x "$D/fps_present_1920" ]; then
    echo "ERROR: missing 1920x1080 presentation probe: $D/fps_present_1920" >&2
    exit 1
fi
if [ ! -r "$D/libwayland_client_real.so.1" ]; then
    echo "ERROR: missing required Wayland client library: $D/libwayland_client_real.so.1" >&2
    exit 1
fi

exec env XDG_RUNTIME_DIR=/run WAYLAND_DISPLAY=wayland-4 HOME=/root \
    "$A/lib/ld-linux.so.3" --library-path "$LP" \
    "$D/fps_present_1920"
