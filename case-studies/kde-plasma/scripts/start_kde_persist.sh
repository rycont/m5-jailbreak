#!/bin/sh
A=/opt/usr/a/armel
T=/tmp/stubs
LPR=$T:$A/lib:$A/usr/lib/arm-linux-gnueabi:$A/usr/lib:/usr/lib
LP=$A/lib:$A/usr/lib/arm-linux-gnueabi:$A/usr/lib:/usr/lib
export XDG_RUNTIME_DIR=/run TV_WAYLAND_DISPLAY=wayland-0
export QT_PLUGIN_PATH=$A/usr/lib/arm-linux-gnueabi/qt6/plugins
export QML2_IMPORT_PATH=$A/usr/lib/arm-linux-gnueabi/qt6/qml
export QML_IMPORT_PATH=$A/usr/lib/arm-linux-gnueabi/qt6/qml
export XDG_DATA_DIRS=$A/usr/share:/usr/share
export KDE_FULL_SESSION=true KDE_SESSION_VERSION=6 XDG_CURRENT_DESKTOP=KDE
rm -f /run/wayland-9 /run/wayland-9.lock /run/wayland-3 /run/wayland-3.lock /tmp/dbus-kde
setsid env LD_LIBRARY_PATH=$LPR $A/lib/ld-linux.so.3 --library-path $LPR /tmp/relay_kde_shm wayland-9 > /tmp/relay_final.log 2>&1 < /dev/null &
for i in $(seq 1 30); do grep -q "listening wayland-9" /tmp/relay_final.log 2>/dev/null && break; sleep 1; done
setsid env -u DBUS_SESSION_BUS_ADDRESS LD_LIBRARY_PATH=$LP WAYLAND_DISPLAY=wayland-9 KWIN_FORCE_OWN_QPA=1 KWIN_COMPOSE=Q QT_QPA_PLATFORM=wayland $A/lib/ld-linux.so.3 --library-path $LP $A/usr/bin/kwin_wayland --socket wayland-3 > /tmp/kwin_final.log 2>&1 < /dev/null &
for i in $(seq 1 30); do grep -q "Accepting client" /tmp/kwin_final.log 2>/dev/null && break; sleep 1; done
/usr/bin/dbus-daemon --session --fork --address=unix:path=/tmp/dbus-kde 2>/dev/null
export DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/dbus-kde
setsid env LD_LIBRARY_PATH=$LP WAYLAND_DISPLAY=wayland-9 $A/lib/ld-linux.so.3 --library-path $LP $A/usr/lib/arm-linux-gnueabi/libexec/kactivitymanagerd > /tmp/kactivity_final.log 2>&1 < /dev/null &
sleep 3
setsid env LD_LIBRARY_PATH=$LP WAYLAND_DISPLAY=wayland-3 QT_QPA_PLATFORM=wayland QT_QUICK_BACKEND=software $A/lib/ld-linux.so.3 --library-path $LP $A/usr/bin/plasmashell > /tmp/plasmashell_final.log 2>&1 < /dev/null &
(while true; do /tmp/osdprobe2 >/dev/null 2>&1; sleep 300; done) &
