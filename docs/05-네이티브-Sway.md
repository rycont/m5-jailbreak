# 04 · 네이티브 Sway

목표: 미러·캡처 루프 없이 Sway가 `/dev/dri/card0`로 패널에 직접 스캔아웃하게 만든다.

## 구성

- Sway **1.10.1**
- wlroots **0.18.2**
- 렌더러 **pixman** (CPU) — GBM이 없으므로
- DRM 백엔드 **legacy** (`WLR_DRM_NO_ATOMIC=1`) — plane 0개
- dumb buffer 스캔아웃
- `LD_PRELOAD` shim 1개

## 1. trixie 준비

Debian trixie armhf rootfs를 `/opt/usr/a/trixie`에 풉니다. `sway`, `foot`, `kcalc`, `grim` 등을 설치합니다. chroot 권한이 없으므로 실행은 호스트에서 로더만 바꿔서 합니다([00](00-준비.md)).

```sh
patchelf --set-interpreter /opt/usr/a/trixie/lib/ld-linux-armhf.so.3 \
  /opt/usr/a/trixie/usr/bin/sway
```

## 2. shim 빌드

`case-studies/native-sway/drmcap.c`를 armhf로 빌드합니다.

```sh
arm-linux-gnueabihf-gcc -shared -fPIC -O2 -nostdlib \
  -o libdrmcap.so drmcap.c
install -m 755 libdrmcap.so /opt/usr/a/libdrmcap.so
```

`-nostdlib`가 링크 오류를 내면 그 옵션만 빼면 됩니다. 이 파일은 libc를 쓰지 않고 `svc`로만 syscall합니다.

shim이 하는 일은 세 가지입니다.

1. `drmGetCap(0x12)` — `DRM_CAP_CRTC_IN_VBLANK_EVENT`를 지원한다고 거짓 보고. wlroots가 이벤트를 받도록.
2. `drmModeSetCursor` / `MoveCursor` / `SetCursor2` — 조용히 성공 반환. ndp에 커서 plane이 없음.
3. `drmHandleEvent` — page-flip 이벤트의 `crtc_id=0`을 **20**으로 바꿔 핸들러에 전달. wlroots 0.18은 0을 매칭하지 못함.

## 3. 실행

`case-studies/native-sway/drmsway.sh`:

```sh
export XDG_RUNTIME_DIR=/opt/usr/a/trixie/run
export WLR_BACKENDS=drm
export WLR_DRM_DEVICES=/dev/dri/card0
export WLR_RENDERER=pixman
export WLR_DRM_NO_ATOMIC=1
export WLR_LIBINPUT_NO_DEVICES=1
export WLR_NO_HARDWARE_CURSORS=1
export LIBSEAT_BACKEND=builtin
export LD_PRELOAD=/opt/usr/a/libdrmcap.so
export LD_LIBRARY_PATH=/opt/usr/a/trixie/usr/lib/arm-linux-gnueabihf:/opt/usr/a/trixie/lib/arm-linux-gnueabihf:/opt/usr/a/trixie/usr/lib:/opt/usr/a/trixie/lib
export PATH=/opt/usr/a/trixie/usr/bin:/usr/bin:/bin
export XDG_DATA_DIRS=/opt/usr/a/trixie/usr/share:/usr/share
export TERMINFO=/usr/share/terminfo
exec /opt/usr/a/trixie/usr/bin/sway -c /tmp/m5drm.config
```

`m5drm.config`는 foot를 왼쪽 960x1080, KCalc를 오른쪽 960x1080에 고정합니다.

```ini
output * bg #1a1a1a solid_color
default_border none
exec_always /opt/usr/a/trixie/usr/bin/bash -c "... foot ... /tmp/drmfoot.sh"
exec_always /opt/usr/a/trixie/usr/bin/bash -c "QT_QPA_PLATFORM=wayland ... kcalc"
for_window [app_id="foot"] floating enable, resize set 960 1080, move position 0 0
for_window [app_id="org.kde.kcalc"] floating enable, resize set 960 1080, move position 960 0
```

`drmfoot.sh`는 1초마다 화면을 지우고 시각을 찍는 루프입니다. 라이브 갱신 확인용.

## 4. 확인

```sh
export SWAYSOCK=$(ls /opt/usr/a/trixie/run/sway-ipc.* | head -1)
/opt/usr/a/trixie/usr/bin/swaymsg -t get_outputs
```

기대값: `LVDS-1 1920x1080 @ 60Hz`, `current mode 1920x1080`.

추가 확인:

- `ps`에 `sway`, `foot`, `kcalc`가 살아 있음
- 패널 캡처를 1초 간격으로 두 장 떠서 md5가 다름 (시계 갱신)
- `GETCRTC`의 `fb_id`가 Sway가 만든 dumb buffer fb를 가리킴

실측 캡처: `case-studies/native-sway/evidence-native-sway-1342.png`.

## 5. 왜 이 조합인가

| 선택 | 이유 |
|---|---|
| pixman | GBM/TBM 경로가 없어 GPU 텍스처 공유 불가 |
| legacy KMS | plane 0개 → atomic 불가 |
| dumb buffer | `CREATE_DUMB`/`MAP_DUMB`는 정상 동작 |
| `LIBSEAT_BACKEND=builtin` | seatd/logind 없음 |
| `WLR_LIBINPUT_NO_DEVICES=1` | 입력 장치가 우리 네임스페이스에 없음 |
| shim | 위 3가지 커널/드라이버 특이점 우회 |

## 6. 알려진 한계

- 입력 없음: 화면만 Sway, 키보드/마우스는 벤더 쪽
- 3D 가속 없음: pixman CPU 렌더
- 재부팅 시 master를 다시 빼앗아야 함
- E 프로세스는 계속 살아 있음 (죽일 수 없음)
