#!/usr/bin/env python3
"""Async HTTP client test against a running vela instance."""
import argparse
import asyncio
import json
import socket
import sys
import time
import urllib.parse


async def http_get(host: str, port: int, path: str, timeout: float = 5.0) -> tuple[int, bytes]:
    reader, writer = await asyncio.wait_for(
        asyncio.open_connection(host, port), timeout=timeout
    )
    req = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {host}\r\n"
        f"Connection: close\r\n"
        f"\r\n"
    ).encode()
    writer.write(req)
    await writer.drain()
    data = await asyncio.wait_for(reader.read(), timeout=timeout)
    writer.close()
    try:
        await writer.wait_closed()
    except Exception:
        pass
    head, _, body = data.partition(b"\r\n\r\n")
    status = 0
    first = head.split(b"\r\n", 1)[0]
    parts = first.split()
    if len(parts) >= 2:
        try:
            status = int(parts[1])
        except ValueError:
            status = 0
    return status, body


async def http_post(host: str, port: int, path: str, payload: bytes, timeout: float = 5.0):
    reader, writer = await asyncio.wait_for(
        asyncio.open_connection(host, port), timeout=timeout
    )
    req = (
        f"POST {path} HTTP/1.1\r\n"
        f"Host: {host}\r\n"
        f"Content-Length: {len(payload)}\r\n"
        f"Content-Type: application/octet-stream\r\n"
        f"Connection: close\r\n"
        f"\r\n"
    ).encode() + payload
    writer.write(req)
    await writer.drain()
    data = await asyncio.wait_for(reader.read(), timeout=timeout)
    writer.close()
    try:
        await writer.wait_closed()
    except Exception:
        pass
    head, _, body = data.partition(b"\r\n\r\n")
    status = int(head.split()[1])
    return status, body


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=18090)
    ap.add_argument("--concurrency", type=int, default=32)
    args = ap.parse_args()

    results = {"pass": [], "fail": []}

    def ok(name, cond, detail=""):
        (results["pass"] if cond else results["fail"]).append(
            f"{'PASS' if cond else 'FAIL'}  {name}" + (f"  ({detail})" if detail else "")
        )

    # 1. simple GET
    st, body = await http_get(args.host, args.port, "/")
    ok("GET / status 200", st == 200, f"status={st} body={body[:80]!r}")
    try:
        j = json.loads(body)
        ok("GET / json ok", j.get("ok") is True, str(j))
    except Exception as e:
        ok("GET / json ok", False, str(e))

    # 2. async sleep route
    t0 = time.perf_counter()
    st, body = await http_get(args.host, args.port, "/sleep")
    dt = time.perf_counter() - t0
    ok("GET /sleep status 200", st == 200, f"status={st} {dt*1000:.1f}ms")
    ok("GET /sleep took >= 40ms (asyncio.sleep)", dt >= 0.04, f"{dt*1000:.1f}ms")

    # 3. POST echo
    payload = b"x" * 4096
    st, body = await http_post(args.host, args.port, "/echo", payload)
    ok("POST /echo 200", st == 200, f"status={st}")
    try:
        j = json.loads(body)
        ok("POST /echo body length", j.get("n") == 4096, str(j))
    except Exception as e:
        ok("POST /echo json", False, str(e))

    # 4. concurrent GETs (async client; server is sequential per worker)
    n = args.concurrency
    t0 = time.perf_counter()
    gathered = await asyncio.gather(
        *[http_get(args.host, args.port, "/") for _ in range(n)],
        return_exceptions=True,
    )
    dt = time.perf_counter() - t0
    statuses = []
    errors = 0
    for g in gathered:
        if isinstance(g, Exception):
            errors += 1
        else:
            statuses.append(g[0])
    ok(
        f"concurrent {n} GET / all 200",
        errors == 0 and statuses.count(200) == n,
        f"200={statuses.count(200)} errors={errors} wall={dt*1000:.1f}ms",
    )
    rps = n / dt if dt > 0 else 0
    ok("concurrent throughput recorded", True, f"{rps:.1f} req/s over {dt*1000:.1f}ms")

    # 5. app exception -> 500
    st, body = await http_get(args.host, args.port, "/error")
    ok("GET /error returns 500", st == 500, f"status={st} body={body[:80]!r}")

    print("=== vela async test results ===")
    for line in results["pass"] + results["fail"]:
        print(line)
    print(f"passed={len(results['pass'])} failed={len(results['fail'])}")
    return 0 if not results["fail"] else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
