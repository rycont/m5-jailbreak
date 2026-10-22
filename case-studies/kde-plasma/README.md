# 사례: KDE Plasma 6 (KWin + wl_shm relay)

2026-10-22 밤 ~ 2026-10-23 아침, M5 패널에 KDE Plasma 6.3.6 데스크톱을 띄운 기록입니다.
셸은 KWin 6.3.6 + plasmashell이고, 화면 경로는 **QPainter(CPU 합성) → wl_shm → relay → fb1**입니다.

> **한 줄 요약**: "KWin + Plasma가 실제 패널에 뜬다"는 확인했습니다. **GPU 직접 렌더는 아직 아닙니다.**
> KWin의 OpenGL 장면은 초기화에 실패해 QPainter로 폴백했고, relay 프레젠터만 EGL/GPU를 씁니다.

## 1. 구성

| 요소 | 값 |
|---|---|
| 배포 루트 | `/opt/usr/a/armel` (Debian trixie armel, hard-float) |
| KWin | `/opt/usr/a/armel/usr/bin/kwin_wayland` — **6.3.6**, 1,453,016 B |
| Qt | 6.8.2 |
| Plasma | `plasma-workspace 4:6.3.6-2` |
| 합성 | `KWIN_COMPOSE=Q` (QPainter, CPU) |
| relay | `/tmp/relay_kde_shm` (67,672 B) — Wayland 클라이언트 → `/dev/fb1` |
| 소켓 | relay `wayland-9`, KWin `--socket wayland-3` |
| 출력 | `/dev/fb1` 1920x1080, stride 7680, 8,294,400 B |
| OSD plane | `shown=1`, `order=4` (기본 5) |

라이브 프로세스 예 (2026-10-23 08:31 KST):

```
3792  relay_kde_shm wayland-9
3796  kwin_wayland --socket wayland-3
3828  kactivitymanagerd
3840  plasmashell
3841  sh /tmp/start_kde_fps.sh   ← 감독 셸 1개
```

PID는 재기동마다 바뀝니다. **같은 소켓 이름으로 두 벌을 띄우면 서로를 죽입니다.**

## 2. 화면 경로

```
plasmashell ──wayland-3──▶ KWin (QPainter: CPU로 픽셀 래스터)
                              │ wl_shm 버퍼
                              ▼
                        relay_kde_shm (wayland-9)
                              │ EGL: wl_shm → 텍스처 → FBO
                              ▼
                     /dev/fb1 dmabuf (FBIOGET_DMABUF)
                              │ FBIOPAN_DISPLAY
                              ▼
                        M5 패널 (GP OSD plane, order=4)
```

- relay는 `FBIOGET_DMABUF`로 fb1의 dma-buf를 얻어 EGL 이미지로 감싸고, FBO에 그린 뒤 `FBIOPAN_DISPLAY`로 넘깁니다. **프레젠터 단계는 GPU**입니다.
- 그러나 KWin이 그리는 장면은 QPainter, 즉 CPU 래스터입니다. GBM/KMS 스캔아웃이 아니고, "GPU가 화면에 직접 렌더" 조건에는 못 미칩니다.
- `wl_shm`을 쓰므로 클라이언트 → 컴포지터 구간에 공유 메모리 복사가 있습니다.

## 3. 확인된 것

- **패널 표시**: 07:49, 08:01, 08:15, 08:19, 08:29, 08:31 KST 캡처에서 Breeze 배경 + Plasma 하단 패널 + 갱신되는 시계. 10분 자동 잠금 없음(`kscreenlockerrc`의 `Autolock=false`).
- **KWin 버전/경로**: `kwin_wayland --version` → 6.3.6. Wayland 백엔드로 자기 소켓을 열고 클라이언트를 받음.
- **fb1 포맷**: 1920x1080, stride 7680, 8,294,400 B. 16,588,800 B 캡처는 fb1 전체(2160 높이)입니다.
- **OSD 제어**: `osdctl --query` → `shown=1 order=4`. 복구는 `show 0` → `order 5` 순서.
- **패널 리프레시**: `/dev/fb0` vsync 180회 / 2994.402 ms = **60.112 Hz** (min 15.879, median 16.662, max 17.256 ms).
- **감독 셸 정리**: `start_kde_persist.sh` 4개 → 1개(1461 유지, 360 종료). 이후 08:29 재기동 시 `start_kde_fps.sh` 1개.

## 4. 확인된 실패 / 미완

### 4.1 KWin OpenGL 초기화 — 실패

`KWIN_COMPOSE=O2ES` 및 GBM shim 조합에서 `OpenGL compositing has been successfully initialized`를 **한 번도 보지 못했습니다.**

`/tmp/kwin_opengl.log` (08:26, PID 2383):

```
kwin_wayland_backend: "EGL_KHR_platform_gbm" client extension is not supported by the platform
kwin_scene_opengl: Creating the OpenGL rendering failed:  "Could not initialize egl"
```

`/tmp/kwin_kde7.log`:

```
kwin_core: Attempting to load the OpenGL scene
kwin_scene_opengl: Creating the OpenGL rendering failed:  "Could not initialize egl"
kwin_core: Attempting to load the QPainter scene
kwin_core: QPainter compositing has been successfully initialized
```

원인 후보(전부 미확정):

1. **vendor EGL에 `EGL_KHR_platform_gbm` 없음.** `eglQueryString(EGL_NO_DISPLAY)` 원문은 `EGL_EXT_client_extensions EGL_EXT_platform_base EGL_KHR_client_get_all_proc_addresses`뿐입니다.
2. **렌더 노드 없음.** `/dev/dri/renderD*` 부재 → `--virtual` 백엔드의 OpenGL 후보 제외.
3. **`--drm`은 세션에서 먼저 막힘.** logind/ConsoleKit이 없어 `NoopSession`이 되고 `openRestricted()`가 항상 -1 → `failed to open drm device at "/dev/dri/card0"`.
4. **GBM shim이 KWin 판정에 반영되지 않음.** `eglgbm.so`를 프리로드해도 `hasClientExtension`이 미지원으로 판정. `eglQueryString` 바인딩/프리로드 순서 문제로 추정.
5. 과거 SIGSEGV(01:30경)는 07:47 실행에서 재현되지 않음. 환경 의존, 원인 미확정.

### 4.2 KWin FPS — 유효값 없음

foot을 붙여 `[OSD] shm` 커밋을 10초간 세었으나 `before=mid=after` → **0.00 fps**.
foot은 연결되지만 **KWin이 창을 매핑/합성하지 않아** 프레임이 발생하지 않았습니다(08:18, 08:29 캡처에 창 없음). "0.00"은 목표 미달 수치가 아니라 측정 불가 상태입니다.

### 4.3 KWin 리스너 수명

08:15 이전 KWin(1409)은 약 25분 뒤 새 클라이언트 연결을 거부했습니다(`dpy=(nil)`, foot: "no compositor running?"). 08:15 재기동(3187)은 새 연결을 받았습니다. 원인 미확정.

### 4.4 Plasma Bigscreen

사용자가 2026-10-23 08:09에 **요구를 철회**했습니다. "임의의 KWin + Plasma 환경이면 충족"으로 조건이 바뀌었고, Bigscreen 소스 빌드/이식 조사는 중단했습니다.

## 5. 재현

> ⚠️ `scripts/start_kde_persist.sh`는 시작에서 `/run/wayland-9`(+`.lock`)를 지웁니다.
> 라이브 relay가 wayland-9를 쓰는 중이면 **그 스택이 깨집니다.** 소켓 이름을 바꿔 쓰세요.

```sh
A=/opt/usr/a/armel
LPR=/tmp/stubs:$A/lib:$A/usr/lib/arm-linux-gnueabi:$A/usr/lib:/usr/lib
LP=$A/lib:$A/usr/lib/arm-linux-gnueabi:$A/usr/lib:/usr/lib

export XDG_RUNTIME_DIR=/run TV_WAYLAND_DISPLAY=wayland-0
export QT_PLUGIN_PATH=$A/usr/lib/arm-linux-gnueabi/qt6/plugins
export QML2_IMPORT_PATH=$A/usr/lib/arm-linux-gnueabi/qt6/qml
export XDG_DATA_DIRS=$A/usr/share:/usr/share

$A/lib/ld-linux.so.3 --library-path $LPR /tmp/relay_kde_shm wayland-9 > /tmp/relay_final.log 2>&1 &

env -u DBUS_SESSION_BUS_ADDRESS LD_LIBRARY_PATH=$LP WAYLAND_DISPLAY=wayland-9 \
  KWIN_FORCE_OWN_QPA=1 KWIN_COMPOSE=Q QT_QPA_PLATFORM=wayland \
  $A/lib/ld-linux.so.3 --library-path $LP $A/usr/bin/kwin_wayland --socket wayland-3 > /tmp/kwin_final.log 2>&1 &

/usr/bin/dbus-daemon --session --fork --address=unix:path=/tmp/dbus-kde

env LD_LIBRARY_PATH=$LP WAYLAND_DISPLAY=wayland-3 QT_QPA_PLATFORM=wayland QT_QUICK_BACKEND=software \
  $A/lib/ld-linux.so.3 --library-path $LP $A/usr/bin/plasmashell > /tmp/plasmashell_final.log 2>&1 &
```

캡처:

```sh
dd if=/dev/fb1 of=/tmp/fb1.raw bs=7680 count=1080
# 1920x1080, RGBA(AR24) 가정. 상단 1080줄이 표시 영역.
ffmpeg -f rawvideo -pix_fmt rgba -s 1920x1080 -i /tmp/fb1.raw /tmp/fb1.png
```

잠금 해제(부팅 후 자동 잠금 방지):

```
/root/.config/kscreenlockerrc
[Daemon]
Autolock=false
LockOnStartup=false
```

## 6. 증거

- `evidence/panel-0801.png`, `panel-0815.png`, `panel-0829.png` — 실제 패널 캡처
- `checkpoints/` — Turnip의 08:03 / 08:21 / 08:33 체크포인트 원문
- `scripts/` — 인계된 실행 스크립트 7개 (원문)
- 전체 대화 기록: [`archive/2026-10-22`](../../archive/2026-10-22/transcript.md), [`archive/2026-10-23`](../../archive/2026-10-23/transcript.md)

## 7. 다음

1. **EGL/GBM**: `eglgbm.so`가 `eglQueryString`에 반영되지 않는 바인딩 순서 규명. KWin이 libEGL 심볼을 어디서 가져오는지(`/proc/<pid>/maps`, `dlsym` 경로) 확인.
2. **KWin 클라이언트 매핑**: foot/xdgtest가 연결은 되는데 합성되지 않는 이유. KWin debug 로그(`kwin_wayland.debug`)와 configure/commit 흐름.
3. **FPS**: 실제 매핑되는 애니메이션 클라이언트 또는 KWin 자체 애니메이션으로 커밋/초 산출.
4. **세션 브리지**: `--drm` 경로를 쓰려면 logind/ConsoleKit 대체(seatd 등) 또는 `NoopSession::openRestricted` 우회.
