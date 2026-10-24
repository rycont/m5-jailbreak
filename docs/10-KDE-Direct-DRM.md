# 10 · KDE Direct DRM (2026-10-24)

> **Later checkpoint (2026-10-24 20:42 KST):** this document records the earlier
> 11:02 QPainter state. A later direct DRM session is documented in
> [the clockfix checkpoint](../case-studies/kde-direct-drm/notes/CHECKPOINT_2026-10-24_CLOCKFIX.md):
> KWin OpenGL ES 2.0 on Mali-G51 was active, and a full-screen 1920×1080 SHM
> surface reached 59.90 presentation fps. That client changed only a small bar;
> full-frame GPU load and reboot persistence are still pending.

> **정정 (2026-10-24 11:02):** 이 경로는 OpenGL이 아니었다.
> `supportInformation`의 `Compositing Type: QPainter`, 로그의
> `EGL_EXT_platform_base is not supported` / `Could not initialize egl`로
> 확인됐다. QPainter는 패널에 올리지 않기로 한 상태라 QPainter 프로세스를
> 내리고 `start_direct_kde.sh`를 비활성화했다. 아래의 "표시 성공"은
> **direct DRM 표시**일 뿐 OpenGL 성공이 아니다.


`09-KDE-OpenGL-전체화면.md`의 relay 경로와 달리, 이 문서는 **KWin이
`/dev/dri/card0`를 직접 잡고 Plasma를 띄운** 첫 실측입니다. relay 프로세스는
없습니다.

## 결과

- KWin `6.3.6` + Plasma `6.3.6`이 M5 패널에 직접 표시(단, **QPainter 폴백**).
- KWin: `kwin_wayland --drm --socket wayland-4`
- Plasma: `WAYLAND_DISPLAY=wayland-4`
- 캡처: 초록 데스크톱 + 하단 Plasma 패널.
  `case-studies/kde-direct-drm/evidence/m5-kwin-direct-2026-10-24.png`
- 캡처 시점: `kwin=3091` (5분 36초 생존), `plasmashell=538` (45초),
  `kactivitymanagerd=521`. relay 없음.
- **재부팅 재현(10:50)**: 플러그 전원 사이클 → root SSH 복구 →
  `start_direct_kde.sh` 한 번으로 KWin `2945`, plasmashell `3472` 생존.
  패널·시계까지 표시됐지만, 이때도 **QPainter**였다. `evidence/m5-kwin-direct-reboot-2026-10-24.png`.
- **11:02**: QPainter 프로세스를 모두 종료하고 스크립트를 비활성화했다.
- D-Bus `ListNames`에 `org.kde.plasmashell` 등록.

## 막힌 지점과 우회

### 1) `sendCancel` 크래시

Debian armel KWin 6.3.6의 첫 direct 크래시는:

```text
PC=e92d4800  LR=b5f3d7d0  si_addr=e92d4800
```

첫 실행은 `libkwin` base `b59d0000`, `LR=b5f5d7d0` → offset `0x58d7d0`.
debug image로 매핑하니 **`KWin::PointerSwipeGestureV1Interface::sendCancel()`**
내부의 `qmap.h:1120` (`resourceMap().values(...)`)였습니다. PC `e92d4800`은
ARM `push {r11,lr}` 명령 워드로, 함수 포인터/객체가 코드 워드로 오염된
상태입니다.

(`sendCancel` 우회 뒤에는 `libkwin` base `b5a30000`, `LR=b5f3d7d0` →
offset `0x50d7d0` = `initClientExtensions()`로 넘어갔습니다.)

우회: `0x58d6bc`를 `bx lr`로 변경.

### 2) `initClientExtensions` 크래시

`sendCancel`을 우회한 뒤 다음 크래시:

```text
KWin::AbstractEglBackend::initClientExtensions() + 84
Line 114 of qbytearrayalgorithms.h
```

소스는 `abstract_egl_backend.cpp:199`:

```cpp
const char *clientExtensionsCString =
    eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
const QByteArray clientExtensionsString =
    QByteArray::fromRawData(clientExtensionsCString,
                            qstrlen(clientExtensionsCString));
```

여기서 반환 문자열이 잘못된 포인터가 됩니다. 우회: `0x50d77c`를 `bx lr`로
변경해 `m_clientExtensions`를 비운 채 진행. EGL context 생성 자체는
`epoxy_func2.so`의 query shim과 preload 조합으로 통과합니다.

## Debug image

Debian debuginfod에서 **정확한 build-id**의 debug ELF를 받았습니다.

```text
Build ID: ba4770b6b33dfbf42b13a5fc21dd75eefe44a377
URL: https://debuginfod.debian.net/buildid/<id>/debuginfo
local: /tmp/libkwin.debug (74,436,048 bytes)
```

`addr2line -e /tmp/libkwin.debug -f -C 0x50d7d0` /
`gdb ... info symbol`로 위 두 함수를 특정했습니다.

## 패치

`case-studies/kde-direct-drm/scripts/apply_libkwin_patch.py`:

```text
0x58d6bc: 01 ca a0 e3 -> 1e ff 2f e1   (sendCancel)
0x50d77c: 01 ca a0 e3 -> 1e ff 2f e1   (initClientExtensions)
```

적용 대상: `/opt/usr/a/lib/libkwin.so.6`
백업: `/opt/usr/a/lib/libkwin.so.6.bak-direct-20261024`

## 실행

`case-studies/kde-direct-drm/scripts/start_direct_kde.sh` 참고. 핵심 환경:

```sh
A=/opt/usr/a/armel
D=/opt/usr/a/m5kde/direct
KLP=/opt/usr/a/lib:/tmp/kwinwl:/tmp/vgles:/tmp/eglw2:/tmp/kstubs:$A/lib:$A/usr/lib/arm-linux-gnueabi:$A/usr/lib:/usr/lib
PRE=$D/epoxy_func2.so:$D/eglwl_gbm.so:/tmp/eglw2/libEGL.so.1:$D/gbm_cache.so:/tmp/stubs/libgbmshim2.so:$D/noopfd.so:/tmp/stubs/libwlservshim.so:/tmp/stubs/libtbmsym.so

# DBus (Tizen binary; LD_LIBRARY_PATH를 비워야 함)
env -u LD_LIBRARY_PATH /usr/bin/dbus-daemon --session --fork \
  --address=unix:path=/tmp/dbus-kde

# KWin direct
LD_LIBRARY_PATH=$KLP LD_PRELOAD=$PRE \
  KWIN_COMPOSE=O2ES KWIN_DRM_DEVICES=/dev/dri/card0 KWIN_DRM_NO_AMS=1 \
  ... kwin_wayland --drm --no-lockscreen --no-kactivities --socket wayland-4
```

Plasma는 KWin과 달리 **`/tmp/eglw2`를 LD_LIBRARY_PATH에 넣지 않습니다**.
`LP_PLASMA=$A/lib:$A/usr/lib/arm-linux-gnueabi:$A/usr/lib:/usr/lib` +
`QT_QUICK_BACKEND=software`를 사용합니다.

## 검증 명령

```sh
ps -eo pid,etime,stat,args | grep -E '[k]win_wayland --drm|[p]lasmashell|[k]activitymanagerd'
DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/dbus-kde dbus-send --session \
  --dest=org.freedesktop.DBus --type=method_call --print-reply / \
  org.freedesktop.DBus.ListNames
```

## 남은 문제

- 1920x1080@60 미측정이며, QPainter 경로는 **금지**라 더 측정하지 않는다.
- OpenGL 경로는 아직 성공하지 않았다. `initClientExtensions` 우회/`hasClientExtension` 강제 모두 실패.
- 첫 캡처의 우측 검은 띠는 재부팅 후 재현되지 않음(일시 현상으로 기록).
- 두 패치는 우회다. 특히 `initClientExtensions`는 `eglQueryString` 반환값의
  근본 원인을 아직 고치지 않았다.
- 재부팅 후 `/tmp`는 비므로 `/opt/usr/a/m5kde/direct`에서 복원해야 한다.
- 재부팅 후 root SSH는 `restore_after_powercycle.sh`로 UEP/dropbear를 다시
  올려야 한다.
