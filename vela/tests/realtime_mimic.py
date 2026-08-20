#!/usr/bin/env python3
"""Live mimic: HTTP traffic against a running vela, then --reload file change."""
import json
import os
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BIN = os.path.join(ROOT, "bin", "vela")
PORT = 18111
APP = os.path.join(ROOT, "examples", "live_app.py")


def http(method, path, body=None, timeout=3.0):
    data = body.encode() if isinstance(body, str) else body
    req = urllib.request.Request(
        f"http://127.0.0.1:{PORT}{path}", data=data, method=method
    )
    if data is not None:
        req.add_header("Content-Type", "application/json")
        req.add_header("Content-Length", str(len(data)))
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.status, r.read().decode()


def wait_up(n=40):
    for _ in range(n):
        try:
            st, _ = http("GET", "/health")
            if st == 200:
                return True
        except Exception:
            time.sleep(0.15)
    return False


def main():
    os.makedirs(os.path.dirname(APP), exist_ok=True)
    with open(APP, "w") as f:
        f.write(
            "MARKER = 'v1'\n"
            "async def application(scope, receive, send):\n"
            "    msg = await receive()\n"
            "    path = scope.get('path') or '/'\n"
            "    n = len(msg.get('body') or b'')\n"
            "    body = ('{\"ok\":true,\"marker\":\"%s\",\"path\":\"' % MARKER) + path + '\",\"n\":' + str('n') + '}\\n'\n"
            "    # fix below in generated? use simple\n"
            "    import json as J\n"
            "    payload = J.dumps({'ok': True, 'marker': MARKER, 'path': path, 'n': n}) + '\\n'\n"
            "    b = payload.encode()\n"
            "    await send({'type':'http.response.start','status':200,'headers':[[b'content-type',b'application/json'],[b'content-length',str(len(b)).encode()]]})\n"
            "    await send({'type':'http.response.body','body': b})\n"
        )
    # rewrite cleanly
    with open(APP, "w") as f:
        f.write(
            "import json\n"
            "MARKER = 'v1'\n\n"
            "async def application(scope, receive, send):\n"
            "    msg = await receive()\n"
            "    path = scope.get('path') or '/'\n"
            "    n = len(msg.get('body') or b'')\n"
            "    payload = json.dumps({'ok': True, 'marker': MARKER, 'path': path, 'n': n}) + '\\n'\n"
            "    b = payload.encode()\n"
            "    await send({\n"
            "        'type': 'http.response.start',\n"
            "        'status': 200,\n"
            "        'headers': [\n"
            "            [b'content-type', b'application/json'],\n"
            "            [b'content-length', str(len(b)).encode()],\n"
            "        ],\n"
            "    })\n"
            "    await send({'type': 'http.response.body', 'body': b})\n"
        )

    env = os.environ.copy()
    env["PYTHONPATH"] = ROOT
    proc = subprocess.Popen(
        [
            BIN,
            "examples.live_app:application",
            "--bind",
            f"127.0.0.1:{PORT}",
            "--workers",
            "1",
            "--reload",
            "--log-level",
            "info",
        ],
        cwd=ROOT,
        env=env,
        stdout=open("/tmp/vela-mimic.log", "w"),
        stderr=subprocess.STDOUT,
    )
    fails = []
    try:
        if not wait_up():
            print(open("/tmp/vela-mimic.log").read())
            print("FAIL server did not start")
            return 1

        st, body = http("GET", "/health")
        j = json.loads(body)
        print("GET /health", st, j)
        if st != 200 or j.get("marker") != "v1":
            fails.append("health v1")

        payload = json.dumps({"user": "mimic", "items": list(range(50))})
        st, body = http("POST", "/ingest", payload)
        j = json.loads(body)
        print("POST /ingest", st, j)
        if j.get("n") != len(payload.encode()):
            fails.append(f"post body n={j.get('n')} expected {len(payload.encode())}")

        ok = 0
        t0 = time.time()
        for i in range(200):
            st, _ = http("GET", f"/tick/{i}")
            if st == 200:
                ok += 1
        dt = time.time() - t0
        print(f"200 sequential GETs: {ok}/200 in {dt:.3f}s ({ok/dt:.0f} rps)")
        if ok != 200:
            fails.append("sequential")

        # live reload: change MARKER
        src = open(APP).read().replace("MARKER = 'v1'", "MARKER = 'v2'")
        with open(APP, "w") as f:
            f.write(src)
        print("wrote MARKER=v2, waiting for --reload ...")
        seen = None
        for _ in range(40):
            time.sleep(0.25)
            try:
                st, body = http("GET", "/health")
                seen = json.loads(body).get("marker")
                if seen == "v2":
                    break
            except Exception:
                continue
        print("after reload marker=", seen)
        if seen != "v2":
            fails.append(f"reload still {seen}")
    finally:
        proc.send_signal(15)
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    print("=== realtime mimic ===")
    if fails:
        print("FAILED", fails)
        return 1
    print("PASSED GET POST burst reload")
    return 0


if __name__ == "__main__":
    sys.exit(main())
