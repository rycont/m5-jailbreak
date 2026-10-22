import os, socket, sys, threading, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tvrun import run_sdb, HOST_IP

local = sys.argv[1]
remote = sys.argv[2]
data = open(local, "rb").read()
res = {}

def serve():
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", 0)); s.listen(4)
    res["port"] = s.getsockname()[1]; s.settimeout(60)
    try:
        c, _ = s.accept(); c.settimeout(20)
        c.sendall(data)
        time.sleep(1)
        c.shutdown(socket.SHUT_WR)
        out = b""
        try:
            while True:
                ch = c.recv(65536)
                if not ch: break
                out += ch
        except socket.timeout:
            pass
        res["data"] = out.decode(errors="replace")
        c.close()
    except Exception as exc:
        res["data"] = "listener error: %r" % (exc,)
    finally:
        s.close()

t = threading.Thread(target=serve, daemon=True); t.start(); time.sleep(1.2)
port = res.get("port", 0)
d = os.path.dirname(remote) or "."
b = os.path.basename(remote)
inner = "cd %s;exec 3<>/dev/tcp/%s/%d;cat<&3>%s" % (d, HOST_IP, port, b)
print("[inner len]", len(inner), repr(inner))
try:
    run_sdb(inner, timeout=90)
except Exception as exc:
    print("sdb:", repr(exc))
t.join(60)
print(res.get("data", ""))
