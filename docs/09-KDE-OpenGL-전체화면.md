# 09 · KDE OpenGL 전체화면 (2026-10-23)

`08-KDE-Plasma.md`의 QPainter 사례를 대체하는 relay 경유 기록입니다.
**2026-10-24 direct DRM 경로는 `10-KDE-Direct-DRM.md`를 보세요.**

## 요약

- KWin 6.3.6 + Plasma 6.3.6이 **KWin OpenGL(Mali-G51)**으로 1920x1080 전체화면 표시.
- KWin `supportInformation`: `OpenGL vendor string: ARM`, `OpenGL renderer string: Mali-G51`, `OpenGL ES 3.2`, `KWIN_COMPOSE=O2ES`.
- 기존 **우하단 4분할/상하반전** 제거. 패널 하단, 시계 정상.
- 60fps는 아직 미달: no-sleep 클라이언트 기준 **18.80 commit/s** (53.2ms/프레임).

## 4분할의 정체 — relay 셰이더 attribute location

KWin이 아니라 **relay의 GLES 셰이더**였다.

```glsl
attribute vec2 p;  // position
attribute vec2 t;  // texcoord
```

relay는 `p`=location 0, `t`=1을 가정하고 `glVertexAttribPointer(0, ..., verts)`, `(1, ..., verts+2)`를 호출했지만, **링크 전에 `glBindAttribLocation`을 하지 않아** Mali가 반대로 배정했다.

- UV(0..1)가 NDC 위치 → **우하단 960x540 사분면**에 그림.
- 위치(-1..1)가 UV → `GL_REPEAT`로 **2x2 타일**, V 부호 때문에 상하반전.

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

## 블랙아웃 — EGLImage 누수

`make_tex_from_surface()`가 프레임마다 `eglCreateImageKHR` + `glGenTextures`를 호출하고 해제하지 않았다. 연속 애니메이션 약 468프레임에서 `[OSD] image fail` → 화면 정지/블랙아웃.
surface별 texture 캐시(최대 8개)로 변경. 900프레임 연속에서 texture 2개, `image fail` 0, commit 915회.

## 검증

- relay FBO에서 **KWin 원본 dmabuf**를 덤프: 초록 배경 + Plasma 패널 + 시계(19:32)가 정상 전체화면.
  - 4분할은 KWin이 아니라 relay 출력에서 발생했음을 확정.
- 수정 relay의 fb1: `captures`의 `fb1_fullscreen_fixed_1946.png`.
- `image fail` 0.

## 60fps 상태

| 항목 | 값 |
|---|---|
| commit/s | **18.80** (16.0초, 301 commits) |
| 프레임 시간 | 53.2ms ≈ 3.2 × 16.67ms |
| KWin CPU | 29.5% |
| relay CPU | 23.2% |

렌더링 포화가 아니라 **present/TBM queue 지연**으로 보임. 다음 단계는 relay의 present 경로 계측.

## 산출물

- `case-studies/kde-plasma-opengl/` — 소스·바이너리·스크립트·보고서·캡처.
- 기기: `/opt/usr/a/relay_kde_shm_fix13`.

## 재현 시 함정

- KWin에 `libEGL.so.1` LD_PRELOAD 금지(segfault), `libwlshim.so` 금지(attach NULL).
- `/tmp/kwinwl/libwayland-client.so.0` ← `/tmp/libwayland-client.so.0.fix6`.
- dbus를 KWin보다 먼저; kactivity/plasmashell에 Qt plugin/QML 경로.
- 재부팅하면 `/tmp` 자산이 사라지므로 `/opt/usr/a`로 영속화 필요.
