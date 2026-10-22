#!/usr/bin/env bash
# m5.sh - bring the Samsung M5 monitor back to a fully working state.
#
# After a reboot two things are lost:
#   * the UEP kernel patch (it lives in RAM)  -> unsigned binaries are blocked again
#   * the dropbear SSH daemon (it was a process) -> we lose our fast command channel
#
# The launcher that applies the patch only runs inside the Tizen app framework,
# so that step goes through SDB; everything else prefers SSH.
set -uo pipefail
cd "$(dirname "$0")"
TV=${M5_HOST:-192.168.0.50}
export M5_HOST="$TV"
PORT=2222
SSH="ssh -i keys/m5_ecdsa -p $PORT -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=15 root@$TV"

echo "== 1) push the helper (via SSH when it is up, otherwise SDB)"
if $SSH true 2>/dev/null; then
  $SSH 'cat > /opt/usr/a/probe.dll; chmod 644 /opt/usr/a/probe.dll' < probe.dll
  echo "   pushed over SSH"
else
  python3 push2.py probe.dll /opt/usr/a/probe.dll | tail -1
fi

echo "== 2) re-apply the UEP bypass (must run inside the app framework -> SDB)"
NOCB=1 python3 tvrun.py '/usr/bin/dotnet-launcher /opt/usr/a/probe.dll uepapply' | tail -1
sleep 50

echo "== 3) (re)start dropbear and install the SSH key"
NOCB=1 python3 tvrun.py '/usr/bin/dotnet-launcher /opt/usr/a/probe.dll sshd' | tail -1
sleep 70

echo "== 4) verify"
NOCB=1 python3 tvrun.py 'cp /tmp/managed_log.txt /tmp/l' >/dev/null 2>&1 || true
python3 tvrun.py 'grep -a apply /tmp/l' 2>/dev/null | tr -s ' ' || true
$SSH 'echo "--- unsigned binary:"; /tmp/busybox echo "BUSYBOX OK as uid $(id -u)" 2>&1; echo "--- internet:"; /tmp/busybox wget -qO- http://ifconfig.me 2>&1 | head -1; echo "--- kernel patch state:"; grep -a apply /tmp/managed_log.txt 2>/dev/null | tail -2' 2>&1 | tail -8
