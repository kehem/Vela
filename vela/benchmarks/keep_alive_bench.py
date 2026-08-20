#!/usr/bin/env python3
"""Keep-alive HTTP/1.1 client. Usage: python3 keep_alive_bench.py host port nreqs conc"""
import asyncio
import sys
import time


async def worker(host, port, n, stats):
    reader, writer = await asyncio.open_connection(host, port)
    got = 0
    req = b"GET / HTTP/1.1\r\nHost: x\r\n\r\n"
    buf = b""
    while got < n:
        writer.write(req)
        await writer.drain()
        while True:
            chunk = await reader.read(65536)
            if not chunk:
                raise RuntimeError("eof")
            buf += chunk
            if b"\r\n\r\n" in buf:
                head, rest = buf.split(b"\r\n\r\n", 1)
                cl = 0
                for line in head.split(b"\r\n")[1:]:
                    if line.lower().startswith(b"content-length:"):
                        cl = int(line.split(b":", 1)[1].strip())
                while len(rest) < cl:
                    rest += await reader.read(cl - len(rest))
                buf = rest[cl:]
                got += 1
                break
    writer.close()
    try:
        await writer.wait_closed()
    except Exception:
        pass
    stats.append(got)


async def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2] if len(sys.argv) > 2 else 18090)
    n = int(sys.argv[3] if len(sys.argv) > 3 else 100000)
    conc = int(sys.argv[4] if len(sys.argv) > 4 else 32)
    per = n // conc
    stats = []
    t0 = time.perf_counter()
    await asyncio.gather(*[worker(host, port, per, stats) for _ in range(conc)])
    dt = time.perf_counter() - t0
    total = sum(stats)
    print(f"requests={total} conc={conc} seconds={dt:.3f} rps={total/dt:.1f}")


if __name__ == "__main__":
    asyncio.run(main())
