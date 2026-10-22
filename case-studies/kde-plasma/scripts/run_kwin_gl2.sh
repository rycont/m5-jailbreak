#!/bin/sh
A=/opt/usr/a/armel
GL=/tmp/glenv
LPR=/tmp/stubs:$A/lib:$A/usr/lib/arm-linux-gnueabi:$A/usr/lib:/usr/lib
LP=$GL:$A/lib:$A/usr/lib/arm-linux-gnueabi:$A/usr/lib:/usr/lib
mkdir -p $GL
ln -sf /usr/lib/libEGL.so.1.4 $GL/libEGL.so.1
ln -sf /usr/lib/libGLESv2.so.2.0 $GL/libGLESv2.so.2
ln -sf /opt/usr/a/lib/libgbm.so.1.0.0 $GL/libgbm.so.1
ln -sf /tmp/stubs/libcapi-system-info.so.0 $GL/libcapi-system-info.so.0
export XDG_RUNTIME_DIR=/run
export QT_PLUGIN_PATH=$A/usr/lib/arm-linux-gnueabi/qt6/plugins
export QML2_IMPORT_PATH=$A/usr/lib/arm-linux-gnueabi/qt6/qml
export QML_IMPORT_PATH=$A/usr/lib/arm-linux-gnueabi/qt6/qml
export XDG_DATA_DIRS=$A/usr/share:/usr/share
rm -f /run/wayland-9 /run/wayland-9.lock /tmp/relay_glrun2.log /tmp/kwin_glrun2.log
setsid env LD_LIBRARY_PATH=$LPR $A/lib/ld-linux.so.3 --library-path $LPR /tmp/relay_kde_shm wayland-9 > /tmp/relay_glrun2.log 2>&1 < /dev/null &
for i in $(seq 1 20); do grep -q "listening wayland-9" /tmp/relay_glrun2.log 2>/dev/null && break; sleep 1; done
setsid env -u DBUS_SESSION_BUS_ADDRESS \
  LD_LIBRARY_PATH=$LP \
  LD_PRELOAD=/tmp/stubs/libgbmshim.so:/tmp/eglfix.so \
  WAYLAND_DISPLAY=wayland-9 \
  KWIN_FORCE_OWN_QPA=1 \
  KWIN_COMPOSE=O2ES \
  QT_QPA_PLATFORM=wayland \
  $A/lib/ld-linux.so.3 --library-path $LP $A/usr/bin/kwin_wayland --socket wayland-3 --no-lockscreen \
  > /tmp/kwin_glrun2.log 2>&1 < /dev/null &
for i in $(seq 1 25); do grep -q "Accepting client" /tmp/kwin_glrun2.log 2>/dev/null && break; sleep 1; done
sleep 2
echo "--- kwin log ---"
cat /tmp/kwin_glrun2.log
echo "--- relay tail ---"
tail -12 /tmp/relay_glrun2.log
echo "--- ps ---"
ps -ef | grep -E "kwin_wayland|relay_kde_shm" | grep -v grep
