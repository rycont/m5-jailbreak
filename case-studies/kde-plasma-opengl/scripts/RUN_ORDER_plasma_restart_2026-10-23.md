# M5 Plasma 스택 재기동 — 실행 순서 (2026-10-23)

대상: Samsung M5 Tizen 5.5 `192.168.200.126:2222` (root, key `keys/m5_ecdsa`)
스크립트: `/opt/usr/a/restart_m5_plasma_2026-10-23.sh` (로컬 사본 `work/m5-solo-2026-10-23/restart_m5_plasma_2026-10-23.sh`)
새 파일이며 기존 `start_kde_persist.sh`는 덮어쓰지 않는다.

## 실행

```sh
ssh -i keys/m5_ecdsa -p 2222 root@192.168.200.126 \
  'sh /opt/usr/a/restart_m5_plasma_2026-10-23.sh'
```

## 순서와 대기 조건

1. **정지** — plasmashell → kactivitymanagerd → kwin_wayland → relay → dbus 순으로
   `pkill`, 10초 대기 후 남은 것 `pkill -9`.
2. **relay** — `/tmp/relay_kde_shm_fix2 wayland-9`
   - env: `XDG_RUNTIME_DIR=/run`, `LD_LIBRARY_PATH=/tmp/stubs:/opt/usr/a/armel/lib:...`
   - 대기: 로그에 `listening wayland-9` (최대 30초)
3. **KWin** — `/opt/usr/a/armel/usr/bin/kwin_wayland --socket wayland-3`
   - 사전: `cp -f /tmp/libwayland-client.so.0.fix6 /tmp/kwinwl/libwayland-client.so.0`
   - env: `WAYLAND_DISPLAY=wayland-9`, `LD_PRELOAD=/tmp/stubs/libwlservshim.so:/tmp/stubs/libgbmshim2.so:/tmp/stubs/libtbmsym.so`,
     `KWIN_FORCE_OWN_QPA=1 KWIN_COMPOSE=O2ES TV_WAYLAND_DISPLAY=wayland-0 QT_QPA_PLATFORM=wayland`
   - 대기: `/run/wayland-3` 소켓 생성 + 2초
4. **session dbus + kactivitymanagerd**
   - `dbus-daemon --session --fork --address=unix:path=/tmp/dbus-kde`
   - kactivity: `WAYLAND_DISPLAY=wayland-9`, `DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/dbus-kde`,
     `QT_QPA_PLATFORM=wayland QT_QUICK_BACKEND=software QT_PLUGIN_PATH=.../qt6/plugins`,
     `QML2_IMPORT_PATH=.../qt6/qml`, `QML_IMPORT_PATH=.../qt6/qml`
5. **plasmashell**
   - env: `WAYLAND_DISPLAY=wayland-3`, `DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/dbus-kde`,
     `QT_QPA_PLATFORM=wayland QT_QUICK_BACKEND=software QT_QPA_PLATFORMTHEME=generic`,
     `QT_PLUGIN_PATH`, `QML2_IMPORT_PATH`, `QML_IMPORT_PATH`, `XDG_DATA_DIRS=/opt/usr/a/armel/usr/share:/usr/share`,
     `KDE_FULL_SESSION=true KDE_SESSION_VERSION=6 XDG_CURRENT_DESKTOP=KDE`
6. **첫 프레임 확인** — relay 로그에 `[OSD] commit` (최대 60초), 마지막에 fb1 1080행을
   `/tmp/fb1_restart_1023.raw`로 덤프하고 md5 출력.

## 검증된 실행 (2026-10-23 15:28:16)

```
[restart 15:28:16] stopping existing stack
[restart 15:28:20] 1/4 relay ok pid=3458
[restart 15:28:24] 2/4 kwin ok pid=3500
[restart 15:28:27] 3/4 kactivity pid=3536
[restart 15:28:27] 4/4 plasmashell
relay_pid=3458 kwin_pid=3500 plasma_pid=3555
osd_commits=1
fb1_md5=4ca504133d7b3795603e89df1c73d3 (첫 프레임 검정)
```

이후 15:30에 `osd_commits=14`, fb1=`44bb7d8f...`로 실제 월페이퍼 프레임 확인.
KWin 3500: `/proc/3500/maps`에 `/dev/mali0` 8개, fd 32=`/dev/mali0`, 41/42=`malitl_3500_*`.

## 함정 (실측)

- **KWin에 `/tmp/eglw2/libEGL.so.1`을 LD_PRELOAD하면 segfault** (startup constructor 이중 초기화).
  libEGL은 `LD_LIBRARY_PATH`로 자연 로드되게 둘 것.
- **KWin에 `/tmp/stubs/libwlshim.so`를 preload하면 `wl_surface.attach(buffer=NULL)`**가 된다.
  relay 로그에 `[ATTACH] buffer=(nil) id=0`이 반복되고 OSD readback이 `000000ff`(검정)로 남는다.
  preload는 `libwlservshim.so:libgbmshim2.so:libtbmsym.so`만 사용.
- **`/tmp/kwinwl/libwayland-client.so.0`은 fix6으로 덮어써야 한다.**
  현재 파일이 fix7 실험본이면 같은 attach NULL이 재현된다.
- **kactivitymanagerd에 Qt plugin/QML 경로가 없으면 abort**한다
  (`Could not find the Qt platform plugin "wayland"`). plasmashell도 같은 경로가 필요하며,
  kactivity가 없으면 `Aborting shell load: The activity manager daemon (kactivitymanagerd) is not running.`로 죽는다.
- relay/OSD는 정상. `[OSD] commit`이 0이면 KWin attach가 NULL인지 먼저 확인할 것.

## 남은 문제 (미해결)

재기동 후에도 화면 우하단에 480x270 데스크톱 미리보기 4개가 항상 다시 나타난다.
animclient 프로세스가 하나도 없어도 나타나므로 animclient 창이 아니다 (상세: REPORT_2026-10-23_1535.md).
패널 시계도 렌더되지 않아 "순수 Plasma + 시계" 캡처는 아직 실패다.
