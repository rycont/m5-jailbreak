#!/usr/bin/env bash
# sshd2.sh - one shot: after a power cycle this installs and starts the SSH
# server on the monitor and verifies a real root login over SSH.
#
#   1) push the helper (probe.dll) and the RSA public key
#   2) re-apply the UEP bypass (the kernel patch lives in RAM, so a reboot
#      clears it)
#   3) run the "sshd" mode of the helper: it binds a writable dir over /root,
#      installs the key and starts the static dropbear with all inherited
#      file descriptors closed (otherwise the daemon keeps the SDB connection)
#   4) log in over SSH as root
set -uo pipefail
cd "$(dirname "$0")"
TV=${M5_HOST:-192.168.0.50}
export M5_HOST="$TV"
PORT=2222
SSH="ssh -i keys/m5_ecdsa -p $PORT -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=15 -o HostKeyAlgorithms=+ssh-rsa root@$TV"

echo "== 1) push helper + key"
python3 push2.py probe.dll /opt/usr/a/probe.dll | tail -1
python3 push2.py keys/m5_ecdsa.pub /opt/usr/home/owner/share/m5_ecdsa.pub | tail -1

echo "== 2) re-apply the UEP bypass"
NOCB=1 python3 tvrun.py '/usr/bin/dotnet-launcher /opt/usr/a/probe.dll uepapply' | tail -1
sleep 50
NOCB=1 python3 tvrun.py 'cp /tmp/managed_log.txt /tmp/l' >/dev/null 2>&1 || true
python3 tvrun.py 'grep -a apply /tmp/l' | tr -s ' '

echo "== 3) install key + start dropbear"
NOCB=1 python3 tvrun.py '/usr/bin/dotnet-launcher /opt/usr/a/probe.dll sshd' | tail -1
sleep 80
NOCB=1 python3 tvrun.py 'cp /tmp/managed_log.txt /tmp/l' >/dev/null 2>&1 || true
python3 tvrun.py 'grep -a sshd /tmp/l' | tr -s ' '

echo "== 4) SSH login as root (key)"
$SSH 'id; uname -a; echo "--- SSH LOGIN OK"' 2>&1 | tail -5

# Password login is set up as a fallback with password "m5" by the helper.
# Use the key above for normal access, or change the hash in root_helper.cs.
