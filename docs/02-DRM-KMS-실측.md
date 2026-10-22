# 02 · DRM/KMS 실측

## 값

`/dev/dri/card0` — `ndp` (Novatek), DRM 1.0.0.

| 항목 | 값 |
|---|---|
| CRTC | 1개, id **20**, active |
| Connector | 1개, id **36**, LVDS, connected |
| Encoder | 1개, id **35** |
| 모드 | 1920x1080@60 |
| Plane | **0개** |
| `CAP_DUMB_BUFFER` | 1 |
| dumb buffer 1080p | 성공, pitch **7680**, 8.29MB |
| `SET_MASTER` (부팅 직후) | `-EINVAL` |

Plane이 0개라 atomic commit은 불가능합니다. wlroots는 이 경우 legacy 경로로 내려가야 하고, Sway에서는 `WLR_DRM_NO_ATOMIC=1`이 필수입니다.

## ioctl 번호

ARM32 기준 `_IOWR` 계산 결과입니다. 여기서 한 번 크게 틀렸기 때문에 표로 남깁니다.

| ioctl | 번호 | 비고 |
|---|---|---|
| `GETRESOURCES` | `0xC04064A0` (nr `0xA0`) | crtc/connector id 목록 |
| `GETCRTC` | `0xC06864A1` (nr `0xA1`) | struct 104바이트 |
| `SETCRTC` | `0xC06864A2` (nr `0xA2`) | **master 전용** |
| `GETENCODER` | `0xC01464A6` (nr `0xA6`) | |
| `GETCONNECTOR` | `0xC05064A7` (nr `0xA7`) | struct 80바이트 |
| `ADDFB` | `0xC01C64AE` (nr `0xAE`) | |
| `PAGE_FLIP` | `0xC01864B0` (nr `0xB0`) | |
| `CREATE_DUMB` | `0xC02064B2` (nr `0xB2`) | |
| `MAP_DUMB` | `0xC01064B3` (nr `0xB3`) | |
| `SET_MASTER` | `0xC004641E` (nr `0x1E`) | |
| `DRM_CAP_CRTC_IN_VBLANK_EVENT` | cap `0x12` | ndp는 미지원으로 보고 |

> 함정: 처음에 connector/CRTC/encoder를 `0x68/0x61/0x63`으로 호출해서 "id=0, ENOENT"라는 틀린 결론을 얻었습니다. `GETRESOURCES`만 맞고 나머지가 틀린 상태였습니다. **번호를 의심하세요.**

## 판독 포인트

### `SET_MASTER`의 EINVAL

"지원 안 함"이 아니라 **"이미 master가 있음"** 입니다. 리눅스 4.1의 `drm_setmaster_ioctl`은 기존 master가 있으면 `-EINVAL`을 돌려줍니다.

### vblank 이벤트의 `crtc_id=0`

ndp는 page-flip 이벤트에 `crtc_id=0`을 넣습니다. wlroots 0.18은 CRTC id 20을 기대하므로 이벤트를 매칭하지 못해 프레임이 멈춥니다. shim이 이벤트를 읽어 20으로 바꿔 넘깁니다([04](04-네이티브-Sway.md)).

### pending flip이 CRTC를 잡아먹는다

완료되지 않은 page flip이 남으면 이후 `PAGE_FLIP`은 `-EBUSY`, `SETCRTC`는 `-EINVAL`이 됩니다. 복구:

1. `SETCRTC(fb=0, count_connectors=0, mode_valid=0)`
2. 원래 vendor fb id로 `SETCRTC` (모드 유지)

이 순서로 풀립니다. `case-studies/native-sway/reset.c`가 이 동작입니다.

### card1

`/dev/dri/card1`은 캡처 전용입니다. `SET_MASTER`는 되지만 `GETRESOURCES`가 `EINVAL`이라 출력용으로 쓸 수 없습니다.

## 최소 프로브

`case-studies/native-sway/modeget.c`가 connector와 모드를 읽고, `kmsown2.c`가 master 상태에서 dumb buffer 스캔아웃까지 수행합니다. 두 파일 모두 libc 없이 syscall로만 작성되어 trixie 로더 없이 Tizen 쪽에서 바로 돌릴 수 있습니다.
