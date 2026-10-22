# Quill → Turnip M5 인계 메모 (2026-10-23 07:56 KST)

작성: Quill. 사용자 지시(07:51, Hazel 경유)로 M5 기기 작업을 중지하고 Turnip 단독 체제로 인계한다.
아래에서 **확인됨**은 내가 기기에서 직접 명령 출력·로그로 본 것, **미확인**은 그렇지 않은 것이다.
Hazel의 캡처/보고는 출처를 명시해 인용했다.

---

## 0. 정리 결과 (내 소유 프로세스 0개)

07:55:11 KST SSH 실측:

```
PID   PPID  ELAPSED  CMD
1385  1     512  ... /tmp/relay_kde_shm wayland-9          ← Turnip, 유지
1409  1     511  ... /opt/usr/a/armel/usr/bin/kwin_wayland --socket wayland-3  ← Turnip, 유지
1460  1     507  ... /opt/usr/a/armel/usr/bin/plasmashell  ← Turnip, 유지
(그 외 relay/kwin/plasmashell 없음)
```

- **내 relay 1680**(wayland-8, 07:47:11 기동)은 07:52 SIGTERM으로 종료. SIGKILL 불필요했음.
- **내 KWin 1732**(gdb, `--socket wayland-4`)는 정리 시점에 이미 없었음. 마지막 로그 07:47:49, Hazel의 07:49:09 확인("wayland-8에 kwin 없음")과 일치. 즉 이번 정리에서 내가 죽인 프로세스가 아니라 그 전에 종료된 상태.
- 내가 만든 잔여 소켓 `/run/wayland-4`, `/run/wayland-8`(+ .lock)은 점유 프로세스가 없음을 `fuser`로 확인한 뒤 삭제.
- 내 06:15 `start_kde_persist.sh` 감독 셸(PID 1854, PGID 1471)과 그 `sleep 300` 루프(PID 3917)도 07:57에 프로세스 그룹 단위로 종료. 이 그룹에는 라이브 relay/kwin이 없었음(setsid로 PPID 1).
- Turnip의 `/run/wayland-3`, `/run/wayland-9`와 PID 1385/1409/1429(kactivitymanagerd)/1460은 **건드리지 않음**.
- 남은 `sh /tmp/start_kde_persist.sh` 4개(360/1461/2385/3623, 07:39~07:45 기동)는 소유자를 확정할 수 없어 **그대로 둠**. 360·1461은 각각 `/tmp/start_kde_out.log`·`/tmp/start_kde_out2.log`로 출력하고, 2385·3623은 SSH 파이프로 출력한다. 라이브 스택(1385/1409/1460)의 감독 셸일 수 있어 손대지 않았다. 필요하면 Turnip이 자기 것으로 확인 후 정리할 것.
- M5 재기동 없음. 새 스택 기동 없음. 30분 자동 보고 트리거(`M5 KWin 30분 진행 보고`, id ca2b9e2e-…)는 07:51에 **일시정지**.
- OSD 상태는 Turnip 라이브 화면 그대로: `osdctl --query` → `shown=1`, `order=4` (07:55). 내가 되돌리지 않았다.

---

## 1. 확인된 성공 (내가 기기에서 직접 검증)

### 1.1 fb1(GP OSD) GPU 직접 출력 — A2 경로
- `/dev/fb1`을 `FBIOGET_DMABUF`로 export → vendor libmali EGL(fbdev winsys) → GLES FBO 렌더 → `FBIOPAN_DISPLAY`. CPU 픽셀 카피 없음.
- 계측: **600 frames / 10.000 s = 60.00 fps**, PAN ioctl 평균 49 µs, free-run 143.19 fps.
- 근거: `m5-a2/M5_A2_GP_PRESENTER_VERIFIED.md`, `m5-a2/a2probe.log`, `m5-a2/a2perf2.log`. 기기 바이너리 `/opt/usr/a/a2probe`, `/opt/usr/a/a2present`.

### 1.2 TBM 재-import 프레젠터 (dma-buf import 확장 없이)
- `FBIOGET_DMABUF(fb1)` → `tbm_bo_import_fd` → `tbm_surface_internal_create_with_bos`(1920x1080 AR24, offset 0 / 8,294,400) → `eglCreateImageKHR(EGL_NATIVE_SURFACE_TIZEN=0x32A1)` → FBO → `FBIOPAN_DISPLAY`.
- 두 버퍼 readback 값 정상(0xffff0099 / 0xff00b233), 10초 600프레임(60.00 fps), pan 평균 45 µs.
- OSD ioctl 주의: `KER_OSD_IO_SET_PLANE_ORDER`(200,0x33)는 **u32 포인터**, `KER_OSD_IO_PLANE_SHOW_ENABLE`(200,0x0E)는 **값을 ulArg에 직접**. 부팅 기본 order=5, GP를 위로 올릴 때 order=4, 복구는 show=0 후 order=5.
- `FBIO_WAITFORVSYNC(arg=0)`는 실제로 블로킹이며 fb0 60.223 Hz / fb1 60.176 Hz(중앙값 16.64 ms)로 측정됨(과거 "논블로킹" 기록 정정).

### 1.3 relay_kde_shm (Wayland→fb1 GPU 프레젠터)
- 내 마지막 실행: `/tmp/relay_kde_shm wayland-8` (env: `WAYLAND_DISPLAY=wayland-8`, `XDG_RUNTIME_DIR=/run`, `LD_LIBRARY_PATH=/tmp/stubs:/opt/usr/a/armel/lib:/opt/usr/a/armel/usr/lib/arm-linux-gnueabi:/opt/usr/a/armel/usr/lib:/usr/lib`). TV(Tizen) 쪽은 기본 wayland-0으로 연결됨(`TV_WAYLAND_DISPLAY` 미설정).
- 로그 `/tmp/relay_gdb2.log` 원문:
```
EGL init ok dpy=0xb67fd268 ctx=0xb68347d0 img=0xb511f6a1 targettex=0xb516a0e9
[OSD] FBIOGET_DMABUF r=0 fd=31 flags=0x2 errno=0
[OSD] fb geom ok 1920x1080 stride=7680 dmabuf=31
[OSD] fb1 1920x1080 stride=7680 size=8294400 dmabuf=31 bo=0xb68eb568 surf=0xb68eb5a0
[OSD] FBO dst status=0x8cd5
[OSD] program=3 link=1
[OSD] listening wayland-8
[DMABUF] bind v4
[DMABUF] feedback sent dev=57856
```
- **내 KWin 1732가 이 relay에 실제로 붙어 xdg surface/toplevel 생성 + `[OSD] shm 1920x1080 stride=7680 fmt=0x1` 커밋**까지 감. 이후 `Mask has hangup flag set ... PID(1732)`.

### 1.4 EGL/platform 사실
- vendor libEGL(`/usr/lib/libEGL.so.1.4`)은 `eglGetProcAddress`로 `eglGetPlatformDisplayEXT`/`eglCreatePlatformWindowSurfaceEXT`를 제공하고 client ext에 `EGL_EXT_platform_base`가 **실제로 있음**. `egllog.so` 인터셉터로 KWin 프로세스 안에서 확인(04:10). 근거: `m5-a2/KWIN_EGL_CONTRADICTION.md`.
- vendor libmali는 `EGL_PLATFORM_GBM_KHR`를 `0x300C (EGL_BAD_PARAMETER)`로 거부. `eglgbm.so`/`eglfix.so`가 GBM platform을 vendor fbdev display로 매핑하고 client ext에 `EGL_KHR_platform_gbm`을 추가 → EGL 1.4 display 획득 성공.
- Tizen libgbm(`/opt/usr/a/lib/libgbm.so.1.0.0`)은 `LD_LIBRARY_PATH=/usr/lib/bufmgr`에서 동작: `gbm_bo_create(1920x1080 ARGB8888)` → fd=18, stride=7680, handle=1. `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)` → FBO OK. `drmModeAddFB2`는 raw gbm handle이면 -6(ENXIO), `drmPrimeFDToHandle(card0)` 후에는 **fb=71 성공**.

### 1.5 KWin 6.3.6 armel 설치
- `/opt/usr/a/armel/usr/bin/kwin_wayland` 1,453,016 B, `libkwin.so.6.3.6`, Qt 6.8.2. `--version` 6.3.6 확인.
- KWin은 Wayland 백엔드로 자기 소켓(`--socket`)을 열고 클라이언트를 받는 단계까지 정상.
- QPainter(`KWIN_COMPOSE=Q`) + wl_shm 경로로 KDE Plasma 데스크톱이 패널에 뜨는 것은 Hazel의 07:49 fb1 캡처로 확인됨(Hazel 증거, 내 캡처 아님). 이 라이브 스택은 지금 Turnip 소유 1385/1409/1460.

---

## 2. 확인된 실패 / 미완 (원문 포함)

### 2.1 KWin OpenGL 장면 초기화 — 실패 (최대 미완 지점)
`KWIN_COMPOSE=O2ES` + shim 시도에서 **`OpenGL compositing has been successfully initialized` 라인을 한 번도 못 봄**.

- `/tmp/kwin_glrun.log` (07:10, `run_kwin_gl.sh`):
```
ERROR: ld.so: object '/tmp/stubs/gbmshim.so' from LD_PRELOAD cannot be preloaded (cannot open shared object file): ignored.
/opt/usr/a/armel/usr/bin/kwin_wayland: symbol lookup error: /opt/usr/a/armel/usr/lib/arm-linux-gnueabi/libQt6WaylandClient.so.6: undefined symbol: wl_proxy_marshal_flags
```
- `/tmp/kwin_glrun2.log` (07:16, `run_kwin_gl2.sh`, `LD_PRELOAD=/tmp/stubs/libgbmshim.so:/tmp/eglfix.so`, `LD_LIBRARY_PATH=/tmp/glenv:armel...`):
```
No backend specified, automatically choosing Wayland because WAYLAND_DISPLAY is set
Accepting client connections on sockets: QList("wayland-3")
```
  → 그 뒤 EGL/OpenGL 로그 없음. 짝 relay `/tmp/relay_glrun2.log`에도 **클라이언트 접속([XDG]) 자체가 없음**(다른 프로브들의 dmabuf bind/hangup만 있음).
- 내 07:47 gdb 실행(`/tmp/kwin_gdb_run.log`, `--socket wayland-4`, shim 없는 armel 기본 경로):
```
MESA-LOADER: failed to open dri: /usr/lib/arm-linux-gnueabi/gbm/dri_gbm.so: cannot open shared object file: No such file or directory (search paths /usr/lib/arm-linux-gnueabi/gbm, suffix _gbm)
kwin_core: Failed to create gbm device for "/dev/dri/card0"
kwin_wayland_backend: Failed to open drm render node "/dev/dri/card0"
```
  이 실행은 죽지 않고 wayland-4 소켓을 연 채 유지됐고, relay wayland-8에 붙어 **wl_shm 프레임**을 보냈다(1.3). 즉 이건 QPainter/wl_shm 경로이고 GPU 직접 렌더가 아니다. Hazel 07:47:50 fb1 캡처는 흰 화면.

### 2.2 KWin `--drm` / `--virtual`이 막히는 구조 (소스+로그 확인)
- `--virtual`: `/dev/dri/renderD*`가 없어 `VirtualBackend::supportedCompositors()`가 OpenGL 후보를 넣지 않음 → QPainter만.
- `--drm`: logind/ConsoleKit이 없어 NoopSession으로 떨어지고 `NoopSession::openRestricted()`가 무조건 -1 → `kwin_wayland_drm: failed to open drm device at "/dev/dri/card0"`로 OpenGL 이전에 실패.
- 상세: `m5-a2/KWIN_EGL_CONTRADICTION.md` §2.

### 2.3 과거 SIGSEGV (환경 의존, 원인 미확정)
`/tmp/kwin_gdb2.log` (01:30경):
```
Program received signal SIGSEGV, Segmentation fault.
0x433d64f8 in ?? ()
#0  0x433d64f8 in ?? ()
#1  0xb67c3e4c in ?? () from /opt/usr/a/armel/usr/lib/ld-linux.so.3
Backtrace stopped: previous frame identical to this frame (corrupt stack?)
```
07:47 실행에서는 재현되지 않음(가드 종료 전까지 생존). 어떤 env 조합에서 죽는지는 **미확인**.

### 2.4 기타 원문 실패
- `/tmp/kwin_fg3.log`: `/opt/usr/a/armel/usr/bin/timeout: /lib/ld-linux.so.3: version 'GLIBC_2.35' not found (required by /opt/usr/a/armel/usr/lib/arm-linux-gnueabi/libc.so.6)` — Tizen 쪽 `timeout`으로 armel 바이너리를 감싸면 안 됨.
- `kwin_noshim_test.sh`(shim 없이 `--version`)는 `libkwin.so.6: undefined symbol: gbm_bo_create_with_modifiers`로 exit 127.

### 2.5 Plasma Bigscreen — 미착수
- `/opt/usr/a/bigscreen` 없음. `/opt` 여유 **568MB**(3.9G 중 3.3G 사용).
- bookworm armel 5.27.5-2 closure는 약 1GB 설치라 공간 부족, 6.7은 Qt 6.10/KF 6.14 필요. (세부 조사는 Turnip 보고 인용, 내가 설치를 시도한 적은 없음.)

---

## 3. 재현 명령

> ⚠️ **주의**: 아래 스크립트 중 `start_kde_persist.sh`, `run_kwin_gl.sh`, `run_kwin_gl2.sh`는 시작 부분에서 `/run/wayland-9`(+`.lock`)을 `rm -f` 한다. 지금 wayland-9는 Turnip 라이브 relay 1385의 소켓이므로 **그대로 실행하면 Turnip 스택이 깨진다.** 반드시 소켓 이름을 바꿔서 쓸 것. (07:45 전후 양쪽이 서로의 스택을 죽인 원인이 이 패턴이다.)

### 3.1 현재 라이브 QPainter KDE 스택 (내 06:15 레시피, 지금은 Turnip 소유)
스크립트 원문: `m5-a2/handover-2026-10-23/start_kde_persist.sh`
핵심:
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

### 3.2 내 relay + KWin(wl_shm) 최소 재현 (07:47, wayland-8)
```sh
A=/opt/usr/a/armel
LPR=/tmp/stubs:$A/lib:$A/usr/lib/arm-linux-gnueabi:$A/usr/lib:/usr/lib
setsid env XDG_RUNTIME_DIR=/run WAYLAND_DISPLAY=wayland-8 QT_QPA_PLATFORM=wayland \
  LD_LIBRARY_PATH=$LPR $A/lib/ld-linux.so.3 --library-path $LPR \
  /tmp/relay_kde_shm wayland-8 > /tmp/relay_gdb2.log 2>&1 < /dev/null &
# KWin 클라이언트 (shim 없음 → QPainter/wl_shm)
setsid env -u DBUS_SESSION_BUS_ADDRESS XDG_RUNTIME_DIR=/run WAYLAND_DISPLAY=wayland-8 \
  KWIN_FORCE_OWN_QPA=1 KWIN_COMPOSE=Q QT_QPA_PLATFORM=wayland \
  LD_LIBRARY_PATH=$A/lib:$A/usr/lib/arm-linux-gnueabi:$A/usr/lib:/usr/lib \
  $A/lib/ld-linux.so.3 --library-path $A/lib:$A/usr/lib/arm-linux-gnueabi:$A/usr/lib:/usr/lib \
  $A/usr/bin/kwin_wayland --socket wayland-4 > /tmp/kwin_gdb_run.log 2>&1 < /dev/null &
```

### 3.3 OpenGL 시도 (07:16 그대로)
스크립트: `m5-a2/handover-2026-10-23/run_kwin_gl2.sh`
```sh
LD_PRELOAD=/tmp/stubs/libgbmshim.so:/tmp/eglfix.so
KWIN_COMPOSE=O2ES KWIN_FORCE_OWN_QPA=1 QT_QPA_PLATFORM=wayland WAYLAND_DISPLAY=<relay 소켓>
LD_LIBRARY_PATH=/tmp/glenv:/opt/usr/a/armel/lib:/opt/usr/a/armel/usr/lib/arm-linux-gnueabi:/opt/usr/a/armel/usr/lib:/usr/lib
```
`/tmp/glenv`에는 `libEGL.so.1 → /usr/lib/libEGL.so.1.4`, `libGLESv2.so.2 → /usr/lib/libGLESv2.so.2.0`, `libgbm.so.1 → /opt/usr/a/lib/libgbm.so.1.0.0`, `libcapi-system-info.so.0 → /tmp/stubs/...` 심볼릭 링크가 있음.

### 3.4 OSD/plane 제어
```sh
/opt/usr/a/osdctl --query      # shown / order
/opt/usr/a/osdctl show 1       # GP plane 표시
/opt/usr/a/osdctl order 4      # GP top (기본 5 = Tizen OSDP top)
/opt/usr/a/kmsown2             # E의 DRM master를 죽이지 않고 비움 (Sway/KWin DRM용)
```

---

## 4. 경로 목록

### 기기 (M5, root@192.168.200.126:2222, key /volume1/homes/rycont/m5-autorecover/keys/m5_ecdsa)
- 바이너리: `/tmp/relay_kde_shm` (67,672 B, 04:36), `/opt/usr/a/armel/` (armel 루트), `/opt/usr/a/osdctl`, `/opt/usr/a/kmsown2`, `/opt/usr/a/a2present`, `/opt/usr/a/a2probe`
- shim: `/opt/usr/a/eglgbm.so`, `/opt/usr/a/gbmshim.so`, `/opt/usr/a/egllog.so`, `/tmp/eglfix.so`, `/opt/usr/a/crashlog.so`, `/opt/usr/a/crashmin.so`, `/opt/usr/a/libdrmcap.so`
- 라이브러리 트릭: `/tmp/stubs/` (libeglcompat.so, libeglshim.so, libgbmshim.so, libgbmproxy.so, libtbm.so.1.0.0, libwayland-{client,server,egl,tbm-client,tbm-server}.so, libdmaimportshim.so, libwlshim.so, libwlservshim.so, libtbmsym.so, libcrashbb.so, libcapi-system-info.so.0), `/tmp/kstubs/`, `/tmp/kwinlibs/`, `/tmp/glenv/`
- vendor: `/usr/lib/libEGL.so.1.4`, `/usr/lib/libGLESv2.so.2.0`, `/opt/usr/a/lib/libgbm.so.1.0.0` (TBM libgbm), `/usr/lib/bufmgr`
- 로그(내 것): `/tmp/relay_gdb2.log`, `/tmp/kwin_gdb_run.log`, `/tmp/kwin_glrun2.log`, `/tmp/relay_glrun2.log`, `/tmp/kwin_glrun.log`, `/tmp/relay_glrun.log`, `/tmp/kwin_gdb2.log`, `/tmp/kwin_fg3.log`
- 로그(Turnip 라이브, 내가 건드리지 않음): `/tmp/relay_final.log`, `/tmp/kwin_final.log`, `/tmp/plasmashell_final.log`, `/tmp/kactivity_final.log`
- 스크립트 사본: `/tmp/start_kde_persist.sh`, `/tmp/run_kwin_gl2.sh`, `/tmp/run_kwin_gl.sh`, `/tmp/kwin_native_test.sh`, `/tmp/kwin_noshim_test.sh`, `/tmp/run_guarded.sh`, `/tmp/kdrm.sh`

### 호스트 (내 작업 폴더)
- `/root/.openmausbot/task-workspaces/2738c052-2ef0-481a-a289-3ad394da36c8/e4f888ad-b331-45fe-a13d-d5d50c3964bb/m5-a2/`
  - `M5_A2_GP_PRESENTER_VERIFIED.md` (A2 60fps 검증)
  - `M5_KWIN_NATIVE_STATUS.md` (01:30 KWin 상태·SIGSEGV)
  - `KWIN_EGL_CONTRADICTION.md` (EGL platform_base 규명, --virtual/--drm 원인)
  - 소스: `a2present.c`, `a2probe.c`, `eglgbm.c`, `eglfix.c`, `gbmshim.c`, `egllog.c`, `osdctl.c`, `tbmeglprobe.c`, `addfbprobe.c`, `kwin_egl_probe.c`
  - `handover-2026-10-23/` — 위 실행 스크립트 7개 원문

---

## 5. 남은 미검증 / Turnip이 이어받을 것

1. **KWin OpenGL 장면 초기화**: relay 소켓에 붙여 `OpenGL compositing has been successfully initialized` + 60초 생존 + 실제 GPU 렌더 프레임. 내 시도는 전부 QPainter/wl_shm 또는 초기화 전 정지.
2. **내 KWin 1732 종료 원인**: 크래시 라인 없음. 가드 타임아웃으로 종료된 것으로 보이나 확정 아님.
3. **Bigscreen**: 미설치(공간 568MB). 5.27 armel 설치 또는 6.7 빌드 판단 필요.
4. **KWin 1080p60 패널 계측**: KWin 경로로는 아직 없음. (A2 프레젠터 자체는 60.00fps 실측)
5. **KDE 표시 증거**: 07:49 패널 캡처는 Hazel의 것이고 QPainter+wl_shm이다. GPU 직접 렌더 증거로 쓰면 안 됨.
6. `KWIN_COMPOSE=O2ES`에서 KWin이 relay에 아예 접속하지 않는 이유(플러그인/스레드 초기화 단계 정지인지) — 로그에 단서 없음.

---

## 6. 인계 시 주의

- Turnip 라이브 소켓은 `/run/wayland-9`(relay 1385), KWin 자기 소켓은 `wayland-3`(1409). 새 실험은 **다른 소켓 이름**(예: wayland-10)을 쓸 것.
- OSD plane 상태(`shown=1 order=4`)는 Turnip 라이브 화면이다. 실험 후 되돌릴 때 `show 0` → `order 5` 순서(부팅 기본 5).
- `/tmp/stubs`·`/tmp/kstubs`·`/tmp/kwinlibs`·`/tmp/glenv`는 공용이다. 내가 지우지 않았다.
- Tizen 쪽 `/usr/bin/timeout`, `/bin/sh`로 armel 바이너리를 감싸지 말 것(GLIBC/ld.so 불일치).
- 내 30분 자동 보고는 꺼져 있다. 내 쪽에서 M5에 새 프로세스를 띄우지 않는다.
