# 08 · KDE Plasma 6 사례

Sway 다음 사례로, M5 패널에 **KDE Plasma 6.3.6 데스크톱**을 띄웠습니다.
셸 조건(KWin + Plasma)은 충족했지만, **GPU 직접 렌더와 60fps 실측은 아직 미달**입니다.

## 구조

```
plasmashell → KWin 6.3.6 (QPainter, CPU) → wl_shm → relay_kde_shm
                                                    │ EGL FBO
                                                    ▼
                                            /dev/fb1 dmabuf → FBIOPAN_DISPLAY
```

- KWin은 Wayland 백엔드(`--socket wayland-3`)로 뜨고, relay(`wayland-9`)가 그 화면을 fb1로 옮깁니다.
- `KWIN_COMPOSE=Q`이므로 장면 합성은 CPU입니다. relay의 EGL은 **프레젠터에만** 쓰입니다.
- 즉 "KWin + Plasma가 패널에 보인다"와 "GPU가 직접 렌더한다"는 별개이며, 전자만 확인됐습니다.

## 확인된 값

| 항목 | 값 |
|---|---|
| KWin | 6.3.6 (armel), Qt 6.8.2 |
| Plasma | `plasma-workspace 4:6.3.6-2` |
| 출력 | fb1 1920x1080, stride 7680, 8,294,400 B |
| OSD | `shown=1`, `order=4` |
| 패널 리프레시 | 60.112 Hz (fb0 vsync 180회 / 2994.402 ms) |
| 잠금 | `Autolock=false`, `LockOnStartup=false` |

## 막힌 지점

- **OpenGL**: `kwin_scene_opengl: Creating the OpenGL rendering failed: "Could not initialize egl"`.
  vendor EGL에 `EGL_KHR_platform_gbm`이 없고, 렌더 노드(`/dev/dri/renderD*`)도 없습니다.
  `--drm`은 세션(`NoopSession::openRestricted()` → -1)에서 먼저 막힙니다.
- **FPS**: KWin이 foot 창을 매핑하지 않아 프레임이 발생하지 않았습니다(10초 delta 0). 유효한 KWin FPS 값은 아직 없습니다.
- **셸**: 일반 Plasma 데스크톱입니다. Plasma Bigscreen 요구는 2026-10-23 사용자 결정으로 철회됐습니다.

## 재현

소켓 이름을 고정하고, 기존 스택을 정리한 뒤 한 세트만 띄웁니다. 전체 스크립트와 원문 로그는
[`case-studies/kde-plasma`](../case-studies/kde-plasma/README.md)에 있습니다.

```sh
A=/opt/usr/a/armel
LPR=/tmp/stubs:$A/lib:$A/usr/lib/arm-linux-gnueabi:$A/usr/lib:/usr/lib
LP=$A/lib:$A/usr/lib/arm-linux-gnueabi:$A/usr/lib:/usr/lib
export XDG_RUNTIME_DIR=/run TV_WAYLAND_DISPLAY=wayland-0
export QT_PLUGIN_PATH=$A/usr/lib/arm-linux-gnueabi/qt6/plugins
export QML2_IMPORT_PATH=$A/usr/lib/arm-linux-gnueabi/qt6/qml
export XDG_DATA_DIRS=$A/usr/share:/usr/share

$A/lib/ld-linux.so.3 --library-path $LPR /tmp/relay_kde_shm wayland-9 > /tmp/relay.log 2>&1 &
env -u DBUS_SESSION_BUS_ADDRESS LD_LIBRARY_PATH=$LP WAYLAND_DISPLAY=wayland-9 \
  KWIN_FORCE_OWN_QPA=1 KWIN_COMPOSE=Q QT_QPA_PLATFORM=wayland \
  $A/lib/ld-linux.so.3 --library-path $LP $A/usr/bin/kwin_wayland --socket wayland-3 > /tmp/kwin.log 2>&1 &
/usr/bin/dbus-daemon --session --fork --address=unix:path=/tmp/dbus-kde
env LD_LIBRARY_PATH=$LP WAYLAND_DISPLAY=wayland-3 QT_QPA_PLATFORM=wayland QT_QUICK_BACKEND=software \
  $A/lib/ld-linux.so.3 --library-path $LP $A/usr/bin/plasmashell > /tmp/plasmashell.log 2>&1 &
```

## 다음

1. EGL `eglQueryString`/프리로드 바인딩 규명 → `EGL_KHR_platform_gbm` 인식
2. KWin이 xdg 클라이언트를 매핑하지 않는 원인
3. 실제 매핑되는 애니메이션으로 KWin FPS 계측
4. `--drm`용 세션 브리지(seatd/logind 대체)
