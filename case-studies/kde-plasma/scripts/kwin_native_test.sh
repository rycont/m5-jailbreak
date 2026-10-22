#!/bin/sh
A=/opt/usr/a/armel; L=$A/lib/ld-linux.so.3
LP=/opt/usr/a/vendor:/usr/lib/bufmgr:$A/usr/lib/arm-linux-gnueabi:$A/lib/arm-linux-gnueabi
export QT_PLUGIN_PATH=$A/usr/lib/arm-linux-gnueabi/qt6/plugins
export QML2_IMPORT_PATH=$A/usr/lib/arm-linux-gnueabi/qt6/qml
export XDG_DATA_DIRS=$A/usr/share:/usr/share
export XDG_RUNTIME_DIR=/run
export KWIN_FORCE_OWN_QPA=1
export LD_PRELOAD=/opt/usr/a/gbmshim.so:/opt/usr/a/eglgbm.so
export KWIN_COMPOSE=O2ES
export KWIN_DRM_NO_AMS=1
echo "== version"
LC_ALL=C.UTF-8 $L --library-path $LP $A/usr/bin/kwin_wayland --version
echo "== virtual"
$L --library-path $LP $A/usr/bin/kwin_wayland --virtual --no-lockscreen --socket wayland-kwin > /tmp/kwin_native_v.log 2>&1 &
KPID=$!
sleep 12
echo "kwin pid $KPID alive: $(kill -0 $KPID 2>/dev/null && echo yes || echo no)"
ls -la /run/wayland-kwin* 2>/dev/null
kill -9 $KPID 2>/dev/null
wait $KPID 2>/dev/null
echo "== log"
tail -30 /tmp/kwin_native_v.log
