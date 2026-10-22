# 03 · DRM master 탈취

Sway가 패널에 직접 쓰려면 `/dev/dri/card0`의 DRM master여야 합니다. 부팅 직후 master는 벤더 스택이 쥐고 있고, 우리 PID 네임스페이스 밖이라 죽일 수 없습니다. 그래서 커널 쪽 `SET_MASTER` 경로를 우회합니다.

## 먼저 확인: master가 비어 있는가

`SET_MASTER`를 호출해 봅니다.

- `0` → 획득 성공. 이 fd가 master입니다.
- `-EINVAL` → 이미 master가 있음. 아래 패치 경로.

`/dev/ntkgpu`가 있으면 `drm_minor`의 `master` 필드(`+0x34`)를 직접 읽어 확인할 수도 있습니다. 이 부팅에서는 `minor = 0x27B3E280`, 필드는 `0x27B3E2B4`였습니다.

> 중요: master를 잡은 fd를 닫으면 `minor->master`가 NULL이 됩니다. 즉 **획득 자체가 벤더 쪽 master를 지우는 부작용**을 가집니다. 복원 절차를 반드시 준비하세요.

## 경로 A — master가 비어 있을 때

이미 이전 실험이나 정상 종료로 비어 있다면, `open("/dev/dri/card0", O_RDWR)`만으로 master가 됩니다. 그다음 master 전용 `SETCRTC`가 성공하는지 확인합니다.

```sh
/opt/usr/a/kmsown2
# GETCRTC 0 fb=... 1920x1080 valid=1
# SETCRTC(ours) 0
```

`kmsown2.c`는 dumb buffer에 색 막대를 채워 스캔아웃하고, 40회 샘플링해 벤더가 fb를 되찾아가는지 관찰한 뒤 vendor fb로 복원합니다.

## 경로 B — 벤더가 쥐고 있을 때 (커널 패치)

`/dev/ntkgpu`는 물리 페이지를 `mmap`할 수 있는 프리미티브입니다. `mmap(NULL, 0x1000, ..., fd, phys>>12)` 형태로 물리 주소를 직접 읽고 씁니다.

### 이 부팅에서 찾은 주소

| 대상 | 주소 |
|---|---|
| `drm_setmaster_ioctl` | virt `0xC02BA764` / phys `0x022BA764` |
| core ioctl 테이블의 `0x641e` 엔트리 | phys `0x2535B40` |
| `drm_minor` | phys `0x27B3E280` |
| `minor->master` | phys `0x27B3E2B4` (`+0x34`) |
| 벤더 master 객체 | virt `0xE5B4A680` |

### 패치 3개

원래 32비트 워드를 백업한 뒤, `drm_setmaster_ioctl` 안에서 의미를 이렇게 바꿉니다.

| 위치 | 원래 | 변경 |
|---|---|---|
| phys `0x022BA798` | `cmp r7, #0` | `cmp r7, r7` (항상 같음 → 검사 통과) |
| phys `0x022BA7A0` | `ldr r0, [r4, #0x48]` | `ldr r0, [r8, #0x34]` |
| phys `0x022BA7B0` | `str r0, [r8, #0x34]` | `str r0, [r4, #0x48]` |

그다음 `DRM_IOCTL_SET_MASTER`를 호출하면 `0`이 돌아오고, 이어서 master 전용 `SETCRTC`도 `0`이 됩니다. 실측에서 동일 fb/모드로 `SETCRTC`가 통과했습니다.

### 복원

1. 패치 3개를 원래 워드로 되돌립니다.
2. `minor->master`에 벤더 객체를 되돌려 씁니다: phys `0x27B3E2B4` ← `0xE5B4A680`.
3. fd를 닫습니다.

이 순서를 지키지 않으면 벤더가 master를 잃은 채로 남고, 화면 갱신이 멈출 수 있습니다.

## 주소는 부팅마다 다릅니다

위 주소는 **특정 부팅의 값**입니다. 커널이 같은 펌웨어라도 KASLR/할당 순서에 따라 달라질 수 있습니다. 실행 전에:

1. `drm_setmaster_ioctl` 심볼 또는 ioctl 테이블 엔트리를 다시 찾고
2. `drm_minor`와 `master` 필드를 다시 읽고
3. 원래 워드를 백업한 뒤 패치하세요.

`kmsown2.c`의 `rd32`/`wr32`가 그 프리미티브의 최소 구현입니다. 주소만 바꿔 재사용할 수 있습니다.

## 윤리/안전

이 경로는 커널 코드를 실행 중에 고칩니다. 잘못된 주소에 쓰면 즉시 패닉 또는 브릭입니다. **백업, 원복 워드 기록, 재부팅 복구 경로**가 준비되지 않았으면 하지 마세요. 이 저장소는 개인 소유 기기에서의 기록입니다.
