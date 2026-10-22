#!/bin/sh
A=/opt/usr/a/armel
L=$A/lib/ld-linux.so.3
LP=/tmp/kwinlibs:/tmp/kstubs:$A/usr/lib/arm-linux-gnueabi:$A/lib/arm-linux-gnueabi
export LD_LIBRARY_PATH=$LP
export LD_PRELOAD=/opt/usr/a/egllog.so:/opt/usr/a/gbmshim.so:/tmp/stubs/libeglcompat.so:/tmp/stubs/libwlservshim.so:/tmp/stubs/libwlshim.so
export KWIN_FORCE_OWN_QPA=1
export XDG_RUNTIME_DIR=/run
exec "$L" --library-path "$LP" "$A/usr/bin/kwin_wayland" --drm --no-lockscreen --socket wayland-2
