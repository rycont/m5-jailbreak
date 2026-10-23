#!/bin/sh
# M5 (Samsung Tizen, 192.168.200.126) KDE Plasma stack restart
# Created 2026-10-23 15:10 KST. New file (does not replace start_kde_persist.sh).
# Requires the /tmp assets that are currently live on the device:
#   /tmp/relay_kde_shm_fix2, /tmp/stubs, /tmp/kwinwl, /tmp/vgles, /tmp/eglw2, /tmp/kstubs
# Usage (on device):  sh /opt/usr/a/restart_m5_plasma_2026-10-23.sh
set -u

A=/opt/usr/a/armel
LP_RELAY=/tmp/stubs:$A/lib:$A/usr/lib/arm-linux-gnueabi:$A/usr/lib:/usr/lib
LP_KWIN=/tmp/kwinwl:/tmp/vgles:/opt/usr/a/lib:/tmp/eglw2:/tmp/kstubs:$A/lib:$A/usr/lib/arm-linux-gnueabi:$A/usr/lib:/usr/lib
LP_PLASMA=$A/lib:$A/usr/lib/arm-linux-gnueabi:$A/usr/lib:/usr/lib
PRE_KWIN=/tmp/stubs/libwlservshim.so:/tmp/stubs/libgbmshim2.so:/tmp/stubs/libtbmsym.so
QP=$A/usr/lib/arm-linux-gnueabi/qt6/plugins
QM=$A/usr/lib/arm-linux-gnueabi/qt6/qml
LOG_R=/tmp/relay_restart_1023.log
LOG_K=/tmp/kwin_restart_1023.log
LOG_P=/tmp/plasmashell_restart_1023.log
LOG_A=/tmp/kactivity_restart_1023.log

export XDG_RUNTIME_DIR=/run
export HOME=/root
export PATH=/usr/bin:/bin:/sbin:/usr/sbin

say() { echo "[restart $(date +%H:%M:%S)] $*"; }

say "stopping existing stack"
pkill -f '/opt/usr/a/armel/usr/bin/plasmashell' 2>/dev/null
pkill -f 'kactivitymanagerd' 2>/dev/null
pkill -f 'kwin_wayland --socket wayland-3' 2>/dev/null
pkill -f '/tmp/relay_kde_shm_fix2' 2>/dev/null
pkill -f 'dbus-daemon --session --fork --address=unix:path=/tmp/dbus-kde' 2>/dev/null
i=0
while [ $i -lt 10 ]; do
    pgrep -f '/opt/usr/a/armel/usr/bin/plasmashell' >/dev/null 2>&1 || \
    pgrep -f 'kwin_wayland --socket wayland-3' >/dev/null 2>&1 || break
    sleep 1; i=$((i+1))
done
pkill -9 -f '/opt/usr/a/armel/usr/bin/plasmashell' 2>/dev/null
pkill -9 -f 'kwin_wayland --socket wayland-3' 2>/dev/null
pkill -9 -f '/tmp/relay_kde_shm_fix2' 2>/dev/null
sleep 1

rm -f /run/wayland-9 /run/wayland-9.lock /run/wayland-3 /run/wayland-3.lock
rm -f /tmp/dbus-kde
: > "$LOG_R"; : > "$LOG_K"; : > "$LOG_P"; : > "$LOG_A"

say "1/4 relay (wayland-9, OSD -> fb1)"
setsid env LD_LIBRARY_PATH=$LP_RELAY XDG_RUNTIME_DIR=/run \
    $A/lib/ld-linux.so.3 --library-path $LP_RELAY \
    /tmp/relay_kde_shm_fix2 wayland-9 > "$LOG_R" 2>&1 < /dev/null &
i=0
while [ $i -lt 30 ]; do grep -q 'listening wayland-9' "$LOG_R" 2>/dev/null && break; sleep 1; i=$((i+1)); done
if ! grep -q 'listening wayland-9' "$LOG_R" 2>/dev/null; then say "FAIL relay"; tail -20 "$LOG_R"; exit 1; fi
say "relay ok pid=$(pgrep -f '/tmp/relay_kde_shm_fix2' | head -1)"

say "2/4 kwin_wayland (wayland-3, EGL/GBM via /dev/mali0 + /dev/dri/card0)"
cp -f /tmp/libwayland-client.so.0.fix6 /tmp/kwinwl/libwayland-client.so.0
setsid env -u DBUS_SESSION_BUS_ADDRESS \
    LD_LIBRARY_PATH=$LP_KWIN LD_PRELOAD=$PRE_KWIN XDG_RUNTIME_DIR=/run WAYLAND_DISPLAY=wayland-9 \
    KWIN_FORCE_OWN_QPA=1 KWIN_COMPOSE=O2ES TV_WAYLAND_DISPLAY=wayland-0 QT_QPA_PLATFORM=wayland \
    $A/lib/ld-linux.so.3 --library-path $LP_KWIN \
    $A/usr/bin/kwin_wayland --socket wayland-3 > "$LOG_K" 2>&1 < /dev/null &
i=0
while [ $i -lt 30 ]; do [ -S /run/wayland-3 ] && break; sleep 1; i=$((i+1)); done
if [ ! -S /run/wayland-3 ]; then say "FAIL kwin socket"; tail -30 "$LOG_K"; exit 1; fi
sleep 2
say "kwin ok pid=$(pgrep -f 'kwin_wayland --socket wayland-3' | head -1)"

say "3/4 session dbus + kactivitymanagerd"
/usr/bin/dbus-daemon --session --fork --address=unix:path=/tmp/dbus-kde 2>/tmp/dbus_restart_1023.err
i=0
while [ $i -lt 10 ]; do [ -S /tmp/dbus-kde ] && break; sleep 1; i=$((i+1)); done
export DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/dbus-kde
setsid env LD_LIBRARY_PATH=$LP_PLASMA XDG_RUNTIME_DIR=/run WAYLAND_DISPLAY=wayland-9 \
    DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/dbus-kde \
    QT_QPA_PLATFORM=wayland QT_QUICK_BACKEND=software QT_QPA_PLATFORMTHEME=generic \
    QT_PLUGIN_PATH=$QP QML2_IMPORT_PATH=$QM QML_IMPORT_PATH=$QM XDG_DATA_DIRS=$A/usr/share:/usr/share \
    KDE_FULL_SESSION=true KDE_SESSION_VERSION=6 XDG_CURRENT_DESKTOP=KDE \
    $A/lib/ld-linux.so.3 --library-path $LP_PLASMA \
    $A/usr/lib/arm-linux-gnueabi/libexec/kactivitymanagerd > "$LOG_A" 2>&1 < /dev/null &
sleep 3
say "kactivity pid=$(pgrep -f kactivitymanagerd | head -1)"

say "4/4 plasmashell (Qt Wayland client on wayland-3, software Qt paint; KWin composites on Mali)"
setsid env LD_LIBRARY_PATH=$LP_PLASMA XDG_RUNTIME_DIR=/run WAYLAND_DISPLAY=wayland-3 \
    DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/dbus-kde \
    QT_QPA_PLATFORM=wayland QT_QUICK_BACKEND=software QT_QPA_PLATFORMTHEME=generic \
    QT_PLUGIN_PATH=$QP QML2_IMPORT_PATH=$QM QML_IMPORT_PATH=$QM XDG_DATA_DIRS=$A/usr/share:/usr/share \
    KDE_FULL_SESSION=true KDE_SESSION_VERSION=6 XDG_CURRENT_DESKTOP=KDE \
    $A/lib/ld-linux.so.3 --library-path $LP_PLASMA \
    $A/usr/bin/plasmashell > "$LOG_P" 2>&1 < /dev/null &
i=0
while [ $i -lt 30 ]; do pgrep -f '/opt/usr/a/armel/usr/bin/plasmashell' >/dev/null && break; sleep 1; i=$((i+1)); done

say "waiting for first composited frame in relay log"
i=0
while [ $i -lt 60 ]; do grep -q '\[OSD\] commit' "$LOG_R" 2>/dev/null && break; sleep 1; i=$((i+1)); done

say "status"
echo "relay_pid=$(pgrep -f '/tmp/relay_kde_shm_fix2' | head -1)"
echo "kwin_pid=$(pgrep -f 'kwin_wayland --socket wayland-3' | head -1)"
echo "plasma_pid=$(pgrep -f '/opt/usr/a/armel/usr/bin/plasmashell' | head -1)"
echo "osd_commits=$(grep -c '\[OSD\] commit' "$LOG_R" 2>/dev/null)"
echo "last_osd=$(grep '\[OSD\] commit' "$LOG_R" 2>/dev/null | tail -1)"
echo "plasma_errors=$(grep -c 'Loading shell integration failed' "$LOG_P" 2>/dev/null)"
# full fb1 page (visible 1920x1080 page, stride 7680)
dd if=/dev/fb1 bs=7680 count=1080 of=/tmp/fb1_restart_1023.raw 2>/dev/null
echo "fb1_md5=$(md5sum /tmp/fb1_restart_1023.raw | cut -d' ' -f1)"
say "done"
