#!/usr/bin/env python3
"""
tizen_sdb.py - raw SDB (Smart Development Bridge) client for Samsung Tizen devices.

Why raw: Samsung's own `sdb` CLI cannot send every verb the TV's sdbd understands,
and the device accepts only ONE SDB connection at a time (a second one is reset),
so this client talks to the device directly, one command per connection.

Usage:
  tizen_sdb.py info
  tizen_sdb.py exec "0 getduid"
  tizen_sdb.py duid
  tizen_sdb.py applist
  tizen_sdb.py launch <app_id>
  tizen_sdb.py push <local> <remote>      (delegates to the sdb binary next to this file)
  tizen_sdb.py pull <remote> <local>      (delegates to the sdb binary next to this file)
  tizen_sdb.py ports [host]                (quick port scan)

Options: --host IP --port 26101
"""
import base64
import json
import os
import socket
import struct
import subprocess
import sys
import time

A_CNXN = 0x4E584E43
A_OPEN = 0x4E45504F
A_OKAY = 0x59414B4F
A_CLSE = 0x45534C43
A_WRTE = 0x45545257
NAMES = {A_CNXN: "CNXN", A_OPEN: "OPEN", A_OKAY: "OKAY", A_CLSE: "CLSE", A_WRTE: "WRTE"}

DEFAULT_HOST = os.environ.get("M5_HOST", "192.168.0.50")
DEFAULT_PORT = 26101


class Raw:
    """Minimal SDB protocol client (24 byte header: cmd, arg0, arg1, len, sum, ~cmd)."""

    def __init__(self, host, port, timeout=8):
        self.sock = socket.create_connection((host, port), timeout)
        self.sock.settimeout(timeout)
        self.buf = b""
        self.send(A_CNXN, 0x01000000, 4096, b"host::\x00")
        self.cnxn = self.read()

    def send(self, cmd, arg0, arg1, payload=b""):
        head = struct.pack(
            "<6I", cmd, arg0, arg1, len(payload), sum(payload) & 0xFFFFFFFF, cmd ^ 0xFFFFFFFF
        )
        self.sock.sendall(head + payload)

    def _read_n(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise EOFError("device closed the connection")
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def read(self):
        cmd, a0, a1, length, _ck, _mg = struct.unpack("<6I", self._read_n(24))
        payload = self._read_n(length) if length else b""
        return cmd, a0, a1, payload

    def open_stream(self, dest, local_id=7):
        self.send(A_OPEN, local_id, 0, dest.encode() + b"\x00")
        return self.read()

    def write(self, remote_id, data):
        self.send(A_WRTE, 0, remote_id, data)

    def pump(self, seconds=4):
        out = []
        end = time.time() + seconds
        self.sock.settimeout(1.0)
        while time.time() < end:
            try:
                cmd, a0, a1, data = self.read()
            except socket.timeout:
                continue
            except EOFError:
                out.append(b"")
                break
            if cmd == A_WRTE:
                out.append(data)
            elif cmd == A_CLSE:
                break
        return b"".join(out)

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass


def connect(host, port, retries=3):
    last = None
    for attempt in range(retries):
        try:
            return Raw(host, port)
        except Exception as exc:  # device resets a second concurrent session
            last = exc
            time.sleep(1.5)
    raise last


def sdb_exec(host, port, command, payload=b"", wait=4):
    raw = connect(host, port)
    try:
        cmd, _a0, a1, _data = raw.open_stream("shell:" + command)
        if cmd != A_OKAY:
            return b""
        if payload:
            raw.write(a1, payload)
        return raw.pump(wait)
    finally:
        raw.close()


def sdb_push_pull(args, host, port):
    """push/pull need the file-sync service; the sdb binary implements it."""
    binary = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sdb")
    if not os.path.exists(binary):
        raise SystemExit("sdb binary not found next to this script")
    env = dict(os.environ, HOME=os.environ.get("SDB_HOME", "/tmp/sdbhome"))
    os.makedirs(env["HOME"], exist_ok=True)
    subprocess.run([binary, "connect", f"{host}:{port}"], env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return subprocess.run([binary] + args, env=env).returncode


def port_scan(host, ports):
    import concurrent.futures

    def check(p):
        s = socket.socket()
        s.settimeout(0.8)
        try:
            s.connect((host, p))
            return p
        except Exception:
            return None
        finally:
            s.close()

    found = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=200) as ex:
        for res in ex.map(check, ports):
            if res:
                found.append(res)
    return found


def main():
    host, port = DEFAULT_HOST, DEFAULT_PORT
    argv = sys.argv[1:]
    while argv and argv[0].startswith("--"):
        key = argv.pop(0)
        if key == "--host":
            host = argv.pop(0)
        elif key == "--port":
            port = int(argv.pop(0))
        else:
            raise SystemExit(f"unknown option {key}")
    if not argv:
        print(__doc__)
        return

    cmd = argv.pop(0)

    if cmd == "info":
        raw = connect(host, port)
        c = raw.cnxn
        print("device banner :", c[3].rstrip(b"\x00").decode(errors="replace"))
        print("protocol ver  :", hex(c[1]), "maxdata:", c[2])
        print("serial        :", f"{host}:{port}")
        raw.close()
    elif cmd == "exec":
        out = sdb_exec(host, port, argv[0] if argv else "", payload=" ".join(argv[1:]).encode())
        sys.stdout.write(out.decode(errors="replace"))
    elif cmd == "duid":
        sys.stdout.write(sdb_exec(host, port, "0 getduid").decode(errors="replace"))
    elif cmd == "applist":
        sys.stdout.write(sdb_exec(host, port, "0 vd_applist", wait=8).decode(errors="replace"))
    elif cmd == "launch":
        sys.stdout.write(sdb_exec(host, port, "0 was_execute " + argv[0]).decode(errors="replace"))
    elif cmd in ("push", "pull"):
        raise SystemExit(sdb_push_pull([cmd] + argv, host, port))
    elif cmd == "ports":
        target = argv[0] if argv else host
        ports = list(range(1, 10001)) + list(range(26000, 26220)) + list(range(8000, 8100)) + \
            list(range(9000, 9300)) + list(range(15000, 15600))
        print(port_scan(target, sorted(set(ports))))
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
