#!/bin/sh
A=/opt/usr/a/armel
T=/tmp/stubs
LP=/tmp/kstubs:/tmp/kwinlibs:$T:$A/lib:$A/usr/lib/arm-linux-gnueabi:$A/usr/lib:/usr/lib
export XDG_RUNTIME_DIR=/run
export QT_PLUGIN_PATH=$A/usr/lib/arm-linux-gnueabi/qt6/plugins
export QML2_IMPORT_PATH=$A/usr/lib/arm-linux-gnueabi/qt6/qml
export QML_IMPORT_PATH=$A/usr/lib/arm-linux-gnueabi/qt6/qml
export XDG_DATA_DIRS=$A/usr/share:/usr/share
rm -f /run/wayland-9 /run/wayland-9.lock /tmp/relay_glrun.log /tmp/kwin_glrun.log
setsid env LD_LIBRARY_PATH=$LP $A/lib/ld-linux.so.3 --library-path $LP /tmp/relay_kde_shm wayland-9 > /tmp/relay_glrun.log 2>&1 < /dev/null &
for i in $(seq 1 20); do grep -q "listening wayland-9" /tmp/relay_glrun.log 2>/dev/null && break; sleep 1; done
setsid env -u DBUS_SESSION_BUS_ADDRESS \
  LD_LIBRARY_PATH=$LP \
  LD_PRELOAD=/tmp/stubs/gbmshim.so:/tmp/eglfix.so \
  WAYLAND_DISPLAY=wayland-9 \
  KWIN_FORCE_OWN_QPA=1 \
  KWIN_COMPOSE=O2ES \
  QT_QPA_PLATFORM=wayland \
  $A/lib/ld-linux.so.3 --library-path $LP $A/usr/bin/kwin_wayland --socket wayland-3 --no-lockscreen \
  > /tmp/kwin_glrun.log 2>&1 < /dev/null &
echo "relay:"; tail -3 /tmp/relay_glrun.log
for i in $(seq 1 25); do grep -q "Accepting client" /tmp/kwin_glrun.log 2>/dev/null && break; sleep 1; done
echo "--- kwin log ---"
cat /tmp/kwin_glrun.log
echo "--- relay tail ---"
tail -20 /tmp/relay_glrun.log
ps -ef | grep -E "kwin_wayland|relay_kde_shm" | grep -v grep
