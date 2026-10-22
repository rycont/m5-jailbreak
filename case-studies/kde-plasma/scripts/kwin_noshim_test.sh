#!/bin/sh
A=/opt/usr/a/armel; L=$A/lib/ld-linux.so.3
LP=/opt/usr/a/vendor:/usr/lib/bufmgr:$A/usr/lib/arm-linux-gnueabi:$A/lib/arm-linux-gnueabi
export QT_PLUGIN_PATH=$A/usr/lib/arm-linux-gnueabi/qt6/plugins
export QML2_IMPORT_PATH=$A/usr/lib/arm-linux-gnueabi/qt6/qml
export XDG_DATA_DIRS=$A/usr/share:/usr/share
export XDG_RUNTIME_DIR=/run
export KWIN_FORCE_OWN_QPA=1
unset LD_PRELOAD
echo "== version no shim"
LC_ALL=C.UTF-8 $L --library-path $LP $A/usr/bin/kwin_wayland --version
echo "exit=$?"
