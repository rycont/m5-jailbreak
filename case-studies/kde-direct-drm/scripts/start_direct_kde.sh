#!/bin/sh
# Start KWin/Plasma on the M5 panel through the real DRM backend.
# No relay, no nested compositor, no QPainter.
#
# Prerequisites:
#   - root SSH on 192.168.200.126:2222
#   - /opt/usr/a/m5kde/direct populated from the 2026-10-24 working /tmp
#   - /opt/usr/a/lib/libkwin.so.6 patched by apply_libkwin_patch.py
#
# Run after boot:
#   ssh ... 'sh /opt/usr/a/m5kde/direct/start_direct_kde.sh'
set -u

A=/opt/usr/a/armel
D=/opt/usr/a/m5kde/direct
DBUS=unix:path=/tmp/dbus-kde
SOCK=wayland-4

if pgrep -f '/opt/usr/a/armel/usr/bin/kwin_wayland --drm' >/dev/null 2>&1; then
    echo "direct KWin already running; refusing to start a second one"
    exit 1
fi

# Put the volatile pieces back under /tmp after a reboot.
mkdir -p /tmp/stubs /tmp/eglw2 /tmp/vgles /tmp/kstubs /tmp/kwinwl
cp -a "$D/stubs/." /tmp/stubs/ 2>/dev/null
cp -a "$D/eglw2/." /tmp/eglw2/ 2>/dev/null
cp -a "$D/vgles/." /tmp/vgles/ 2>/dev/null
cp -a "$D/kstubs/." /tmp/kstubs/ 2>/dev/null
cp -a "$D/kwinwl/." /tmp/kwinwl/ 2>/dev/null
cp -f /opt/usr/a/m5kde/libwayland-client.so.0.fix6 /tmp/kwinwl/libwayland-client.so.0 2>/dev/null

python3 "$D/apply_libkwin_patch.py" /opt/usr/a/lib/libkwin.so.6 || exit 1

if [ ! -S /tmp/dbus-kde ]; then
    rm -f /tmp/dbus-kde /tmp/dbus-kde.lock
    setsid env -u LD_LIBRARY_PATH /usr/bin/dbus-daemon \
        --session --fork --address="$DBUS" \
        >/tmp/dbus_direct.log 2>&1
    sleep 2
fi
if [ ! -S /tmp/dbus-kde ]; then
    echo "ERROR: session dbus did not start"
    cat /tmp/dbus_direct.log 2>/dev/null
    exit 1
fi

KLP=/opt/usr/a/lib:/tmp/kwinwl:/tmp/vgles:/tmp/eglw2:/tmp/kstubs:$A/lib:$A/usr/lib/arm-linux-gnueabi:$A/usr/lib:/usr/lib
PRE=$D/epoxy_func2.so:$D/eglwl_gbm.so:/tmp/eglw2/libEGL.so.1:$D/gbm_cache.so:/tmp/stubs/libgbmshim2.so:$D/noopfd.so:/tmp/stubs/libwlservshim.so:/tmp/stubs/libtbmsym.so
QP=$A/usr/lib/arm-linux-gnueabi/qt6/plugins
QM=$A/usr/lib/arm-linux-gnueabi/qt6/qml

rm -f /run/$SOCK /run/$SOCK.lock
setsid env \
    LD_LIBRARY_PATH="$KLP" \
    LD_PRELOAD="$PRE" \
    XDG_RUNTIME_DIR=/run \
    DBUS_SESSION_BUS_ADDRESS="$DBUS" \
    WAYLAND_DISPLAY=wayland-0 \
    TV_WAYLAND_DISPLAY=wayland-0 \
    QT_QPA_PLATFORM=offscreen \
    QT_PLUGIN_PATH="$QP" \
    KWIN_FORCE_OWN_QPA=1 \
    KWIN_COMPOSE=O2ES \
    KWIN_DRM_DEVICES=/dev/dri/card0 \
    KWIN_DRM_NO_AMS=1 \
    "$A/lib/ld-linux.so.3" --library-path "$KLP" \
    "$A/usr/bin/kwin_wayland" --drm --no-lockscreen --no-kactivities \
    --socket "$SOCK" >/tmp/kwin_direct.log 2>&1 </dev/null &

i=0
while [ "$i" -lt 30 ]; do
    [ -S /run/$SOCK ] && break
    sleep 1
    i=$((i + 1))
done
if ! pgrep -f '/opt/usr/a/armel/usr/bin/kwin_wayland --drm' >/dev/null 2>&1; then
    echo "ERROR: direct KWin died"
    tail -30 /tmp/kwin_direct.log 2>/dev/null
    exit 1
fi

LP_PLASMA=$A/lib:$A/usr/lib/arm-linux-gnueabi:$A/usr/lib:/usr/lib
setsid env LD_LIBRARY_PATH="$LP_PLASMA" \
    XDG_RUNTIME_DIR=/run WAYLAND_DISPLAY="$SOCK" \
    DBUS_SESSION_BUS_ADDRESS="$DBUS" \
    QT_QPA_PLATFORM=wayland QT_QUICK_BACKEND=software \
    QT_QPA_PLATFORMTHEME=generic \
    QT_PLUGIN_PATH="$QP" QML2_IMPORT_PATH="$QM" QML_IMPORT_PATH="$QM" \
    XDG_DATA_DIRS=$A/usr/share:/usr/share \
    KDE_FULL_SESSION=true KDE_SESSION_VERSION=6 XDG_CURRENT_DESKTOP=KDE \
    "$A/lib/ld-linux.so.3" --library-path "$LP_PLASMA" \
    "$A/usr/lib/arm-linux-gnueabi/libexec/kactivitymanagerd" \
    >/tmp/ka_direct.log 2>&1 </dev/null &
sleep 2
setsid env LD_LIBRARY_PATH="$LP_PLASMA" \
    XDG_RUNTIME_DIR=/run WAYLAND_DISPLAY="$SOCK" \
    DBUS_SESSION_BUS_ADDRESS="$DBUS" \
    QT_QPA_PLATFORM=wayland QT_QUICK_BACKEND=software \
    QT_QPA_PLATFORMTHEME=generic \
    QT_PLUGIN_PATH="$QP" QML2_IMPORT_PATH="$QM" QML_IMPORT_PATH="$QM" \
    XDG_DATA_DIRS=$A/usr/share:/usr/share \
    KDE_FULL_SESSION=true KDE_SESSION_VERSION=6 XDG_CURRENT_DESKTOP=KDE \
    "$A/lib/ld-linux.so.3" --library-path "$LP_PLASMA" \
    "$A/usr/bin/plasmashell" >/tmp/plasma_direct.log 2>&1 </dev/null &

sleep 12
echo "kwin=$(pgrep -f '/opt/usr/a/armel/usr/bin/kwin_wayland --drm' | head -1)"
echo "plasma=$(pgrep -f '/opt/usr/a/armel/usr/bin/plasmashell' | head -1)"
DBUS_SESSION_BUS_ADDRESS="$DBUS" dbus-send --session \
    --dest=org.freedesktop.DBus --type=method_call --print-reply / \
    org.freedesktop.DBus.ListNames 2>&1 | grep -E 'org.kde.KWin|org.kde.plasmashell' || true
tail -8 /tmp/plasma_direct.log 2>/dev/null
