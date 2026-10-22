# native-sway — 실제 사용한 파일

| 파일 | 설명 |
|---|---|
| `drmcap.c` | wlroots 0.18용 LD_PRELOAD shim. cap 위장, 커서 ioctl 무시, page-flip `crtc_id` 0→20 보정 |
| `libdrmcap.so` | 위 소스를 armhf로 빌드한 바이너리 (참고용) |
| `drmsway.sh` | Sway 실행 환경 |
| `m5drm.config` | 출력 배경, foot/KCalc 배치 |
| `drmfoot.sh` | 1초마다 시각을 찍는 라이브 갱신 확인용 루프 |
| `kmsown2.c` | dumb buffer 스캔아웃 + master 상태 확인 + vendor fb 복원 |
| `reset.c` | wedge된 CRTC 탈출 (fb=0 SETCRTC 후 vendor fb 복원) |
| `restore.c` | 모드/커넥터 조회 및 원복 보조 |
| `modeget.c` | connector/모드 목록 출력 |
| `fliptest.c` | page flip 이벤트 테스트 |
| `evidence-native-sway-1342.png` | 13:42 패널 실측 캡처 |

## 빌드

```sh
arm-linux-gnueabihf-gcc -shared -fPIC -O2 -nostdlib -o libdrmcap.so drmcap.c
arm-linux-gnueabihf-gcc -static -O2 -o kmsown2 kmsown2.c
arm-linux-gnueabihf-gcc -static -O2 -o reset reset.c
```

`kmsown2.c`의 주소 상수(`MINOR`, `MFIELD`, `PATCHBASE`)는 특정 부팅의 값입니다. 그대로 쓰지 마세요.
