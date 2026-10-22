#!/usr/bin/env bash
# uepbypass.sh - restore native (unsigned ELF) execution on the Samsung M5 monitor.
#
# The Tizen SFD/UEP check rejects unsigned ELF binaries.  We patch the return
# value of the UEP verification routine directly in physical RAM through
# /dev/ntkgpu, which makes the kernel treat every binary as signed.
# The patch lives in RAM, so it has to be re-applied after every reboot.
#
# Usage:
#   ./uepbypass.sh                 # apply the patch and verify it
#   ./uepbypass.sh /path/to/bin    # apply the patch, then run that binary on the TV
set -euo pipefail
cd "$(dirname "$0")"

TV=${M5_HOST:-192.168.0.50}
export M5_HOST="$TV"
DLL_LOCAL=probe.dll          # compiled C# helper (see root_helper.cs / UepFix4.cs)
DLL_REMOTE=/opt/usr/a/probe.dll
ELFC=${1:-}

[ -f "$DLL_LOCAL" ] || { echo "probe.dll missing - build it with: dotnet build -c Release"; exit 1; }

echo "== pushing helper to the monitor"
python3 push2.py "$DLL_LOCAL" "$DLL_REMOTE" | tail -1

echo "== applying the UEP bypass (root + kernel patch)"
NOCB=1 python3 tvrun.py "/usr/bin/dotnet-launcher $DLL_REMOTE uepapply" | tail -1
sleep 45

echo "== reading the result"
NOCB=1 python3 tvrun.py 'cp /tmp/managed_log.txt /tmp/l' >/dev/null 2>&1 || true
python3 tvrun.py 'grep -a apply /tmp/l' | tr -s ' '

if [ -n "$ELFC" ]; then
  echo "== pushing and running $ELFC on the monitor"
  python3 push2.py "$ELFC" /tmp/run_me | tail -1
  python3 tvrun.py 'chmod 755 /tmp/run_me; /tmp/run_me; echo EXIT=$?' | tr -s ' '
fi
