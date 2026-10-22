# M5 탈옥

삼성 M5 스마트 모니터(Tizen)에서 벤더 스택을 걷어내고 원하는 리눅스 사용자 공간을 올리는 과정을 기록한 저장소입니다. **Sway 올리기는 그 첫 번째 사례**이고, 이 저장소의 본론은 "이 기기에서 무엇이 열려 있고 무엇이 막혀 있는가"입니다.

> 개인 소유 기기에서만 사용하세요. 아래 절차에는 커널 메모리 패치, DRM master 탈취, 부트 스택이 아닌 영역의 수정이 포함됩니다. **부트로더·fstab·파티션은 건드리지 않는 것을 전제**로 합니다.

## 검증된 기기 사실

2026-10-22, 실제 M5에서 직접 확인한 값입니다.

| 항목 | 값 |
|---|---|
| 커널 | Linux 4.1 (armhf) |
| 사용자 공간 | Tizen (softfp) + `/opt/usr/a/trixie` (Debian trixie, hard-float) |
| 디스플레이 노드 | `/dev/dri/card0` — `ndp` (Novatek), DRM 1.0.0 |
| CRTC | id 20, active, 1920x1080@60 |
| Connector | id 36, LVDS, connected, 1920x1080@60 |
| Encoder | id 35 |
| Plane | **0개** → atomic 불가, legacy modeset/pageflip만 가능 |
| Dumb buffer | `CAP_DUMB_BUFFER=1`, 1080p 할당 성공 (pitch 7680) |
| 캡처 노드 | `/dev/dri/card1` — 캡처 전용 (`SET_MASTER`는 되지만 `GETRESOURCES` EINVAL) |
| 버퍼 할당 | TBM + `libtpl-egl`. **GBM 경로 없음** |
| 기본 컴포지터 | Enlightenment (E), Tizen 확장 사용 |

핵심: **KMS는 껍데기가 아니라 진짜로 돌아갑니다.** 다만 plane이 없어 atomic이 안 되고, 부팅 직후에는 DRM master가 벤더 쪽에 잡혀 있습니다. Sway가 막히는 이유는 KMS 부재가 아니라 **master 소유권 + GBM 부재 + wlroots의 atomic 전제**입니다.

## 현재 되는 것

- SDB 패키지명 인젝션으로 `sdk` 셸, `/dev/ntkgpu` cred 패치로 **root**, dropbear로 **SSH 2222**
- 네이티브 Sway 1.10.1 / wlroots 0.18.2가 M5 패널에 **직접 스캔아웃** (미러·캡처 루프 없음)
- 1920x1080@60, `WLR_RENDERER=pixman`, legacy DRM
- foot(왼쪽) + KCalc(오른쪽) 동시 표시, 라이브 갱신 확인
- 부팅 후 master를 되찾는 두 가지 경로 확보
  - master가 비어 있으면 `open("/dev/dri/card0")`만으로 획득
  - 벤더가 쥐고 있으면 `/dev/ntkgpu` 물리 메모리로 `SET_MASTER` 패치 (주소는 부팅마다 달라짐)

## 현재 안 되는 것

- **입력 포워딩 없음** — 화면만 Sway, 키보드/마우스는 아직 벤더 쪽
- GIMP/Firefox는 trixie에 미설치 (설치 자체는 가능)
- 재부팅하면 벤더가 master를 다시 잡음 → 패치 자동화 필요
- atomic/page flip 이벤트의 `crtc_id=0` 문제를 shim으로 우회 중 (wlroots 0.18 한계)
- SoC 3D 가속 미검증 — 현재는 pixman(CPU) 렌더링

## 문서 지도

| 문서 | 내용 |
|---|---|
| [docs/00-준비.md](docs/00-준비.md) | 백업, SDB 접속, trixie 공간, 안전 수칙 |
| [docs/01-루트와-셸.md](docs/01-루트와-셸.md) | SDB 인젝션 → sdk → cred 패치 → root → UEP → SSH |
| [docs/02-스택-지도.md](docs/02-스택-지도.md) | E, TBM, `drm-service`, ndp, Wayland 계층 관계 |
| [docs/03-DRM-KMS-실측.md](docs/03-DRM-KMS-실측.md) | ioctl 실측값, legacy 모드, 함정 모음 |
| [docs/04-master-탈취.md](docs/04-master-탈취.md) | `SET_MASTER` 우회 3단계와 복원 |
| [docs/05-네이티브-Sway.md](docs/05-네이티브-Sway.md) | shim 빌드, 환경변수, 실행, 확인 |
| [docs/06-복구.md](docs/06-복구.md) | 원복, CRTC wedge 탈출, 브릭 방지 |
| [docs/07-한계와-다음.md](docs/07-한계와-다음.md) | 입력, 가속, 자동화 과제 |

## 사례: root-shell

루트와 셸을 얻는 체인 자체를 도구로 남겼습니다.

- `case-studies/root-shell/tvrun.py` — SDB 패키지명 인젝션(SVE-2025-50109)으로 sdk 명령 실행
- `case-studies/root-shell/root_helper.cs` — `/dev/ntkgpu` 물리 메모리로 `struct cred` 패치, root TCP 셸, UEP 패치, SSH 설치
- `case-studies/root-shell/uep_patch.cs` — UEP 판정 함수 탐색/패치
- `case-studies/root-shell/root_shell.py` — root 셸 클라이언트
- `case-studies/root-shell/sshd2.sh`, `m5.sh` — dropbear SSH 설치와 재부팅 후 복구

## 사례: native-sway

실제로 쓴 소스와 스크립트를 그대로 넣었습니다.

- `case-studies/native-sway/drmcap.c` — LD_PRELOAD shim (cap 위장, 커서 ioctl 무시, `crtc_id` 보정)
- `case-studies/native-sway/drmsway.sh` — Sway 실행 환경
- `case-studies/native-sway/m5drm.config` — 출력/창 배치
- `case-studies/native-sway/kmsown2.c` — 물리 메모리로 master 탈취
- `case-studies/native-sway/restore.c`, `reset.c` — 원복
- `case-studies/native-sway/*.c` — 모드 조회, flip 테스트, 캡처 도구
- `case-studies/native-sway/evidence-native-sway-1342.png` — 패널 실측 캡처

## 경고

이 저장소의 코드는 **특정 펌웨어/부팅의 주소값**을 포함합니다. 커널이 바뀌면 그대로 쓰면 안 됩니다. `kmsown2.c`의 주소는 실행 전에 반드시 다시 찾으세요. 코드 패치 후에는 원복이 기본입니다.
