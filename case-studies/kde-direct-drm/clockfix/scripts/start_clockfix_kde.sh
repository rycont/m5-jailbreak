#!/bin/sh
# Start KWin/Plasma on the M5 panel through the real DRM backend.
# No relay, no nested compositor, no QPainter.
#
# Prerequisites:
#   - root SSH on 192.168.200.126:2222
#   - persistent assets under /opt/usr/a/m5kde/direct
#   - /opt/usr/a/lib/libkwin.so.6 patched by apply_libkwin_patch.py
#
# Run after boot:
#   ssh ... 'sh /opt/usr/a/m5kde/direct/start_clockfix_kde.sh'
set -u

A=/opt/usr/a/armel
D=/opt/usr/a/m5kde/direct
CLOCKFIX=$D/m5_drm_clockfix.so
DBUS=unix:path=/tmp/dbus-kde
SOCK=wayland-4
TIZEN_SOCK=/run/wayland-0

if [ ! -r "$CLOCKFIX" ]; then
    echo "ERROR: missing DRM clockfix shim: $CLOCKFIX"
    exit 1
fi

# The EGL/GBM bridge resolves the fake GBM device through Tizen's Wayland
# display. On a cold boot that server may not exist until Smart Hub is opened.
i=0
while [ ! -S "$TIZEN_SOCK" ] && [ "$i" -lt 180 ]; do
    sleep 1
    i=$((i + 1))
done
if [ ! -S "$TIZEN_SOCK" ]; then
    echo "ERROR: Tizen Wayland socket is not ready: $TIZEN_SOCK"
    echo "Open the Tizen home screen, then run this script again."
    exit 1
fi

if pgrep -f '/opt/usr/a/armel/usr/bin/kwin_wayland --drm' >/dev/null 2>&1; then
    echo "direct KWin already running; refusing to start a second one"
    exit 1
fi

# Put the volatile pieces back under /tmp after a reboot.
mkdir -p /tmp/stubs /tmp/eglw2 /tmp/vgles /tmp/kstubs /tmp/kwinwl /tmp/wltest4
cp -a "$D/stubs/." /tmp/stubs/ 2>/dev/null
cp -a "$D/eglw2/." /tmp/eglw2/ 2>/dev/null
cp -a "$D/vgles/." /tmp/vgles/ 2>/dev/null
cp -a "$D/kstubs/." /tmp/kstubs/ 2>/dev/null
cp -a "$D/kwinwl/." /tmp/kwinwl/ 2>/dev/null
cp -f /opt/usr/a/m5kde/libwayland-client.so.0.fix6 /tmp/kwinwl/libwayland-client.so.0 2>/dev/null
if [ ! -r "$D/libwayland_client_real.so.1" ] || \
   [ ! -r "$D/wlshim_plasma_redirect_nolog.so" ]; then
    echo "ERROR: missing persistent Wayland client assets under $D"
    exit 1
fi
cp -f "$D/libwayland_client_real.so.1" /tmp/wltest4/libwayland_client_real.so.1
ln -sf libwayland_client_real.so.1 /tmp/wltest4/libwayland-client.so.0
if [ ! -r "$D/eglw2/libEGL.so.1.bak3" ]; then
    echo "ERROR: missing known-good EGL wrapper $D/eglw2/libEGL.so.1.bak3"
    exit 1
fi
cp -f "$D/eglw2/libEGL.so.1.bak3" /tmp/eglw2/libEGL.so.1
if [ "$(wc -c < "$D/eglw2/libEGL_real.so.1" 2>/dev/null)" != "14583825" ]; then
    echo "ERROR: unexpected vendor EGL payload size"
    exit 1
fi

if command -v python3 >/dev/null 2>&1; then
    python3 "$D/apply_libkwin_patch.py" /opt/usr/a/lib/libkwin.so.6 || exit 1
else
    echo "python3 absent; using the existing patched /opt/usr/a/lib/libkwin.so.6"
fi

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

KLP=$D:/opt/usr/a/lib:/tmp/kwinwl:/tmp/vgles:/tmp/eglw2:/tmp/kstubs:$A/lib:$A/usr/lib/arm-linux-gnueabi:$A/usr/lib:/usr/lib
# KWin imports epoxy_eglQueryString and its related dispatch entries as OBJECT
# variables containing function pointers. Do not preload epoxy_func2.so here:
# it exports those names as FUNC symbols, which sent KWin through 0xe92d4800.
PRE=$CLOCKFIX:$D/epoxy_dispatch_objects.so:$D/eglwl_gbm.so:/tmp/eglw2/libEGL.so.1:/tmp/stubs/libgbmshim2.so:$D/noopfd.so:/tmp/stubs/libwlservshim.so:/tmp/stubs/libtbmsym.so
QP=$A/usr/lib/arm-linux-gnueabi/qt6/plugins
QM=$A/usr/lib/arm-linux-gnueabi/qt6/qml

rm -f /run/$SOCK /run/$SOCK.lock
rm -f /tmp/m5_drm_clockfix.log
setsid env -i \
    HOME=/root PATH=/usr/bin:/bin:/sbin:/usr/sbin LANG=C.UTF-8 \
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
    --socket "$SOCK" kwin >/tmp/kwin_direct.log 2>&1 </dev/null &

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

# The Wayland socket is created before KWin's DRM/EGL backend is ready. Do not
# start any clients until KWin itself reports active Mali OpenGL composition.
SUPPORT=/tmp/kwin_direct_support.txt
i=0
while [ "$i" -lt 90 ]; do
    if ! pgrep -f '/opt/usr/a/armel/usr/bin/kwin_wayland --drm' >/dev/null 2>&1; then
        echo "ERROR: KWin exited before reporting its OpenGL state"
        tail -30 /tmp/kwin_direct.log 2>/dev/null
        exit 1
    fi
    if ! DBUS_SESSION_BUS_ADDRESS="$DBUS" dbus-send --session \
        --dest=org.kde.KWin --type=method_call --print-reply \
        /KWin org.kde.KWin.supportInformation >"$SUPPORT" 2>&1; then
        :
    elif grep -q 'OpenGL renderer string: Mali-G51' "$SUPPORT" && \
         grep -q 'Compositing is active' "$SUPPORT"; then
        break
    fi
    sleep 1
    i=$((i + 1))
done
if ! grep -q 'OpenGL renderer string: Mali-G51' "$SUPPORT" || \
   ! grep -q 'Compositing is active' "$SUPPORT"; then
    echo "ERROR: KWin never reported active Mali OpenGL composition"
    cat "$SUPPORT" 2>/dev/null
    tail -30 /tmp/kwin_direct.log 2>/dev/null
    exit 1
fi
if ! awk '
    /^Screen [0-9]+:/ { in_screen=1; is_target=0 }
    in_screen && /^Name: LVDS-1$/ { is_target=1 }
    in_screen && is_target && /^Enabled: 1$/ { found=1; exit }
    /^Compositing$/ { in_screen=0 }
    END { exit !found }
' "$SUPPORT"; then
    echo "ERROR: Mali composition is active, but KWin has no enabled LVDS-1 output"
    cat "$SUPPORT" 2>/dev/null
    tail -30 /tmp/kwin_direct.log 2>/dev/null
    exit 1
fi

LP_PLASMA=/tmp/wltest4:/tmp/kwinwl:$D:/opt/usr/a/lib:/tmp/vgles:/tmp/eglw2:/tmp/kstubs:/tmp/stubs:$A/lib:$A/usr/lib/arm-linux-gnueabi:$A/usr/lib:/usr/lib
setsid env LD_LIBRARY_PATH="$LP_PLASMA" \
    LD_PRELOAD="$D/wlshim_plasma_redirect_nolog.so" \
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
    LD_PRELOAD="$D/wlshim_plasma_redirect_nolog.so" \
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
if ! DBUS_SESSION_BUS_ADDRESS="$DBUS" dbus-send --session \
    --dest=org.freedesktop.DBus --type=method_call --print-reply / \
    org.freedesktop.DBus.ListNames 2>/dev/null | grep -q 'org.kde.plasmashell'; then
    echo "ERROR: Plasma did not register on session D-Bus"
    tail -40 /tmp/plasma_direct.log 2>/dev/null
    exit 1
fi
tail -8 /tmp/plasma_direct.log 2>/dev/null
