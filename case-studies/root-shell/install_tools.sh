#!/usr/bin/env bash
# install_tools.sh - push the static ARM binaries the SSH setup needs.
# Run this once after the first root shell works.
set -euo pipefail
cd "$(dirname "$0")"
export M5_HOST="${M5_HOST:-192.168.0.50}"
export M5_HOST_IP="${M5_HOST_IP:-192.168.0.2}"

for f in dropbear dropbearkey busybox-armv7l; do
  [ -f "$f" ] || { echo "missing $f (see README)"; exit 1; }
done

python3 push2.py dropbear      /opt/usr/home/owner/share/dropbear
python3 push2.py dropbearkey   /opt/usr/home/owner/share/dropbearkey
python3 push2.py busybox-armv7l /opt/usr/home/owner/share/busybox

NOCB=1 python3 tvrun.py 'chmod 755 /opt/usr/home/owner/share/dropbear /opt/usr/home/owner/share/dropbearkey /opt/usr/home/owner/share/busybox; ls -l /opt/usr/home/owner/share/dropbear /opt/usr/home/owner/share/busybox'
