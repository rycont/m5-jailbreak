# M5 직접 KWin 진행 메모 — 2026-10-24 20:42 KST

지금까지 한 일과 이어서 볼 일을 다음 대화에서 바로 이어갈 수 있게 적어 둔다.

## 지금까지 확인한 것

- 시계 초가 몇십 초에 한 번 바뀐 증상은 디지털 시계 타이머보다 KWin 출력 갱신 쪽 문제였다. 초마다 repaint를 강제하는 QML 실험은 원인 수정이 아니어서 되돌렸다.
- M5 DRM 직접 조회 결과는 1920×1080, vrefresh=60, totals=2200×1125, clock_khz=148이었다. KWin 6.3.6은 이 clock 값으로 갱신률을 계산해 약 60mHz, 즉 한 프레임에 약 16.7초로 판단했다. 수정 전 KWin PAGEFLIP 간격도 약 15.7~16초였다.
- m5_drm_clockfix.c의 preload shim이 M5 LVDS 모드에서만 KWin이 읽는 clock 148을 148500kHz로 보정한다. drmModeSetCrtc 호출 직전에는 원래 148을 복원해 커널 드라이버에 전달한다. shim 로그에서 GETCONNECTOR/GETCRTC의 보정과 SETCRTC의 원래 값 전달을 확인했다.
- 새 KWin 세션의 supportInformation을 직접 조회했다. KWin 6.3.6, Compositing is active, OpenGL ES 2.0, ARM / Mali-G51, OpenGL ES 3.2, EGL이었다. 현재 KWin 환경은 KWIN_COMPOSE=O2ES이며 /dev/mali0 및 EGL/GLES/TPL 라이브러리가 프로세스에 매핑돼 있다.
- 현재 플라즈마 시계 설정은 showSeconds=2(항상 표시)이고 배경은 KDE Nuvole (Next)다. 시계 QML의 강제 repaint 변경은 남아 있지 않다.

## 측정

- 시계 확인: M5 화면을 1초 간격으로 8회 캡처했고, 8회 모두 성공했다. 각 캡처의 시계 영역 픽셀 해시가 모두 달랐다. 요청 시각 간격은 매번 1.000초였다. 원본 CSV와 PNG는 case-studies/kde-direct-drm/clockfix/evidence/clock-seconds/ 아래에 있다.
- 패널 VSync: /dev/fb1에서 180회 대기, 평균 16.655ms, 60.044Hz. /dev/fb0도 60.181Hz였다. 출력별 원자료는 case-studies/kde-direct-drm/clockfix/evidence/vsync-measurements.txt에 있다.
- 작은 320×180 SHM Wayland 표면: 제출 360, 표시 360, 폐기 0, 59.34fps, missed_vblanks=0.
- 1920×1080 전체 화면 SHM Wayland 표면: 제출 600, 표시 600, 폐기 0, 9.999초 구간에서 59.90fps, refresh_ns=16666666, missed_vblanks=0. 사용자는 측정 중 색 막대가 무지개처럼 화면을 가로질러 움직이는 것을 봤다고 확인했다.

## GPU 여부를 정확히 나눌 것

1920×1080 측정 앱 자체는 EGL/GPU 앱이 아니다. CPU가 wl_shm 버퍼에 색 막대를 썼고, 매 프레임 damage는 128×120 막대 주변에 한정됐다. 확인한 것은 OpenGL ES/Mali 합성이 활성인 KWin이 전체 화면 크기 표면을 패널 VSync마다 거의 60회 표시했다는 점이다. 이 결과만으로 화면 전체 픽셀을 GPU가 매 프레임 다시 그리는 1080p60 부하까지 입증했다고 말하면 안 된다. 그 전면 부하 측정은 남아 있다.

## 현재 실행 상태 / 재현 준비

- 계측 종료 후 KWin PID 962, plasmashell PID 1204, kactivitymanagerd PID 3733이 살아 있고 /run/wayland-4 소켓도 있었다.
- 현재 clockfix .so와 KWin/Plasma 환경 파일은 /tmp에 있다. 재부팅하면 사라질 수 있다. 기존 /opt/usr/a/m5kde/start.sh는 relay 경로를 올리는 스크립트라, 이번 직접 DRM + clockfix 환경과 같은지 확인되지 않았다. clockfix를 포함한 재부팅용 구성은 아직 만들거나 시험하지 않았다.
- 사용자가 씻고 돌아온 뒤 전원 재부팅을 함께 진행하기로 했다. 이번 turn에서는 재부팅과 환경 변경을 하지 않는다.
- B310 플러그의 읽기 전용 조회는 192.168.200.119:5000에서 No route to host였고 ARP도 INCOMPLETE였다. 플러그에 쓰기 프레임이나 전원 토글은 보내지 않았다. 플러그 상태는 미확인이다.

## 이어서 할 일

1. 사용자가 돌아오면 스마트플러그 전원 사이클을 진행하고, M5 root SSH 복구를 확인한다.
2. 기존 persistent /opt/usr/a/m5kde 자산과 현재 /tmp 의존성을 대조해, 직접 KWin + clockfix 환경을 재부팅 뒤 복구할 수 있게 정리한다.
3. 재부팅 뒤 DRM 모드, KWin OpenGL/Mali 상태, 매초 시계 캡처, 1920×1080 presentation을 같은 절차로 재확인한다.
4. 이후 전체 1920×1080 영역을 매 프레임 GPU로 바꾸는 계측을 만들어 실행한다. 성공 전까지 “GPU가 전 화면 1080p60을 그렸다”고 완료 처리하지 않는다.

## 이번 결과 파일

- case-studies/kde-direct-drm/clockfix/src/ — clock shim, DRM 모드 probe, 두 presentation probe, fbdev VSync probe 소스
- case-studies/kde-direct-drm/clockfix/bin/ — M5 ARM용 probe/shim 바이너리
- case-studies/kde-direct-drm/clockfix/evidence/ — KWin GPU supportInformation, shim 로그, 실제 DRM 출력, 1초 캡처, VSync/FPS 출력, 현재 화면 캡처
