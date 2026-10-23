# KDE Plasma OpenGL 전체화면 사례 (2026-10-23)

M5 패널에 **KWin OpenGL(Mali-G51) 전체화면 Plasma 데스크톱**을 띄운 기록입니다.
직전의 QPainter/wl_shm 사례(`docs/08-KDE-Plasma.md`)를 대체하는 최신 상태입니다.

## 결과

- KWin 6.3.6 + Plasma 6.3.6, 1920x1080 전체화면.
- `supportInformation`: `OpenGL vendor string: ARM`, `renderer: Mali-G51`, `OpenGL ES 3.2`, `KWIN_COMPOSE=O2ES`.
- 패널은 하단, 시계 정상. 기존의 **우하단 4분할/상하반전**은 제거됨.
- 900프레임 연속 애니메이션에서 relay `image fail` 0회.

## 핵심 원인

relay `draw_frame()`의 GLES 셰이더 attribute location 미지정.

```glsl
attribute vec2 p;  // position
attribute vec2 t;  // texcoord
```

Mali 링커가 `t`를 location 0, `p`를 1로 배정 → UV가 화면 위치(우하단 0..1), 위치가 UV(-1..1, `GL_REPEAT`)가 되어 **우하단 2x2 타일 + 상하반전**.

수정:

```c
glBindAttribLocation_(prog, 0, "p");
glBindAttribLocation_(prog, 1, "t");
glLinkProgram_(prog);
```

UV는 V만 반전:

```c
static const float verts[]={
  -1,-1, 0,0,   1,-1, 1,0,
  -1, 1, 0,1,   1, 1, 1,1
};
```

## 추가 수정 — EGLImage 누수

`make_tex_from_surface()`가 프레임마다 `eglCreateImageKHR` + `glGenTextures`를 호출하고 해제하지 않아, 연속 468프레임 후 `image fail` → 블랙아웃.
surface별 texture 캐시(최대 8개)로 변경. 900프레임에서 texture 2개, `image fail` 0.

## 파일

| 파일 | 설명 |
|---|---|
| `relay_kde_fix13.c` | 수정된 relay 소스 (attribute bind + texture cache + UV 보정) |
| `relay_fix13` | ARM 정적? 동적 빌드 바이너리 (Tizen armel, glibc) |
| `animclient.c` | wl_shm 애니메이션 클라이언트 |
| `animclient_nosleep` | usleep 패치본 (서버 소화율 측정용) |
| `egl60.c` | EGL/GLES 60Hz 시도 소스 (참고, window surface에서 SIGSEGV) |
| `scripts/restart_m5_plasma_2026-10-23.sh` | 초기 재기동 스크립트 |
| `scripts/RUN_ORDER_plasma_restart_2026-10-23.md` | 실행 순서/함정 |
| `notes/` | 2026-10-23 체크포인트·보고서 전체 |
| `captures/fb1_fullscreen_fixed_1946.png` | 수정 후 전체화면 캡처 |
| `captures/fb1_before_4split.png` | 수정 전 4분할 캡처 |

## 빌드 (NAS 크로스 툴체인)

```sh
TC=/volume1/homes/rycont/m5-build/src/gcc/workspace/tizen5.5_cross_toolchain_vd_muse_target_armv7l_host_x86-64
OV=/volume1/homes/rycont/m5-tpl-2026-10-22/sysroot-full
wayland-scanner server-header .../xdg-shell.xml xdg-shell-server-protocol.h
wayland-scanner private-code  .../xdg-shell.xml xdg-shell-server-protocol.c
wayland-scanner server-header .../linux-dmabuf-unstable-v1.xml linux-dmabuf-server.h
wayland-scanner private-code  .../linux-dmabuf-unstable-v1.xml linux-dmabuf-server.c
$TC/bin/armv7l-tizen-linux-gnueabi-gcc --sysroot=$OV -O2 \
  -o relay_fix13 relay_kde.c xdg-shell-server-protocol.c linux-dmabuf-server.c \
  -lwayland-server -ldl -lpthread -lm
```

## 검증

- KWin 원본 dmabuf를 relay에서 FBO readback으로 덤프 → 초록 배경 + Plasma 패널 + 시계(19:32) **정상 전체화면**.
- fb1 캡처 → 전체화면 + 하단 패널 + 시계 정상 (`captures/fb1_fullscreen_fixed_1946.png`).
- `grep -c "image fail" /tmp/relay_restart2.log` = 0.
- FPS: no-sleep 클라이언트 16초에 301 commit = **18.80/s**, KWin CPU 29.5%, relay CPU 23.2%.
  - 53.2ms/프레임 ≈ 3.2×16.67ms → present/TBM queue 지연 의심. 60fps 미달.

## 재현 시 주의

- KWin에는 `libEGL.so.1`을 LD_PRELOAD하지 말 것 (segfault).
- KWin에는 `libwlshim.so`를 넣지 말 것 (attach NULL).
- `/tmp/kwinwl/libwayland-client.so.0`은 `/tmp/libwayland-client.so.0.fix6`로 덮어쓸 것.
- kactivitymanagerd/plasmashell에는 Qt plugin/QML 경로가 필요.
- dbus를 KWin보다 먼저 띄워 `org.kde.KWin`을 세션 버스에 등록.

## 남은 것

1. 18.8fps → 60fps: relay present/TBM queue 지연 계측.
2. 재부팅 후 `/tmp` 자산 영속화 및 자동 시작.
