# Vela

```
        ╭──────────────╮
   ─────│  the sail    │─────
        ╰──────────────╯
     native C  ·  ASGI 3  ·  Linux
```

**Vela** (Latin *velum*, a sail) is a production-minded **HTTP application server written in C17**. It embeds **CPython** and speaks **ASGI 3**, so Django and FastAPI run behind a native event loop instead of a pure-Python server.

```bash
vela myproject.asgi:application --workers 4 --bind 0.0.0.0:8000
```

One binary. One process tree. Your app stays Python. The sockets, parser, and workers are C.

---

## What it is

Vela sits **behind nginx** (or another proxy) the way gunicorn or uWSGI would. It is **not** a static file CDN and **not** a replacement for nginx itself.

```
                 TLS / HTTP
                     │
                     ▼
              ┌─────────────┐
              │    nginx    │
              └──────┬──────┘
                     │  TCP or unix:/run/vela.sock
                     ▼
              ┌─────────────┐
              │ Vela master │  config · fork · SIGHUP · --reload
              └──────┬──────┘
         ┌───────────┼───────────┐
         ▼           ▼           ▼
      worker      worker      worker
      epoll       epoll       epoll
      HTTP/1.1    HTTP/1.1    HTTP/1.1
      CPython     CPython     CPython
         │           │           │
         └───────────┴───────────┘
                     ▼
              Django / FastAPI
                 (ASGI 3)
```

Each worker is an **isolated process** with its own interpreter. A crash in one app request does not take down the master or the other workers.

---

## Why this shape

| Choice | Trade-off |
|--------|-----------|
| **C + epoll** | Low overhead accept/parse/write. Linux-first. kqueue/IOCP are API-shaped, not shipped. |
| **Embed CPython** | No `python app.py` child. The server *is* the runtime. |
| **ASGI 3** | Same callable uvicorn uses: `async def app(scope, receive, send)`. |
| **Prefork workers** | CPU isolation like gunicorn. One ASGI call at a time **per worker**. Scale with `--workers`. |
| **Unix socket + nginx** | TLS, HTTP/2, buffering, and public bind stay at the proxy. |

Correctness is preferred to fake benchmark numbers. Duplicate `Content-Length` and `Content-Length` + `chunked` are rejected (request smuggling). Bodies are assembled and handed to ASGI as `http.request`. App exceptions become **HTTP 500**; the worker stays up.

---

## Quick start

**Needs:** Linux, GCC (C17), CPython 3.11+ *development* headers, OpenSSL, pthread.

```bash
cd vela
make
make test
./bin/vela --version          # vela 0.1.0

PYTHONPATH=. ./bin/vela examples.hello:application --bind 127.0.0.1:8000
# another terminal
curl http://127.0.0.1:8000/
# hello from vela ASGI
```

Install:

```bash
sudo install -m 755 bin/vela /usr/local/bin/vela
```

CMake (when you have it):

```bash
cmake -B build && cmake --build build && ctest --test-dir build
```

---

## Run your app

The target is **`module:attribute`**, not a file path.

```bash
# Django
vela myproject.asgi:application --workers $(nproc) --unix-socket /run/vela.sock

# FastAPI
vela myapp:app --bind 0.0.0.0:8000 --workers 4

# Hello (in-tree)
PYTHONPATH=. vela examples.hello:application --bind 127.0.0.1:8000
```

### Development reload

```bash
vela myproject.asgi:application --reload --bind 127.0.0.1:8000
```

`--reload` watches `.py` files (nanosecond mtimes), drops `__pycache__`, and **rolling-restarts workers**. It is **not** for production. The master stays up; you do not type anything to reload—save a file.

### Production reload

```bash
kill -HUP $(cat /run/vela.pid)
# or
systemctl reload vela
```

**SIGHUP** starts a new worker (re-imports the app) then SIGTERMs the old one. Works with **one or many** workers. The master never runs the app in-process.

---

## Command line

```
vela APP [OPTIONS]
```

| Flag | What it does |
|------|----------------|
| `--bind HOST:PORT` | TCP listen (repeatable). Default `127.0.0.1:8000` |
| `--unix-socket PATH` | `AF_UNIX` socket; stale file unlinked; mode `0660` |
| `--workers N` | Prefork processes + `SO_REUSEPORT` |
| `--reload` | Dev file watch + rolling restart (**not production**) |
| `--log-level` | `debug` `info` `warning` `error` `critical` |
| `--access-log` / `--error-log` | Path or `-` for stdout |
| `--timeout` / `--keep-alive` | Seconds |
| `--max-connections` | Per-worker cap |
| `--max-request-size` | Body cap (413) |
| `--python-path` / `--working-directory` | `sys.path` / `chdir` |
| `--config FILE` | `key = value` file; **CLI wins** |
| `--pid-file` `--user` `--group` `--daemon` | Service |
| `--ssl-cert` / `--ssl-key` | Reserved (TLS not on the connection path yet) |
| `--help` / `--version` | |

Example config file:

```
workers = 4
bind = "0.0.0.0:8000"
unix_socket = "/run/vela.sock"
keep_alive = 5
request_timeout = 60
max_connections = 10000
log_level = "info"
```

---

## How a request moves

1. **Master** binds nothing for the app logic; it forks workers and waits.
2. **Worker** `accept4`s on edge-triggered epoll (listen sockets stay level-triggered).
3. **HTTP/1.1 parser** (byte state machine, no `sscanf`) reads the request line, headers, `Content-Length` or chunked body into a **reused** buffer.
4. **ASGI scope** is built: `type`, `asgi`, `http_version`, `method`, `scheme`, `path`, `raw_path`, `query_string`, `headers` (lowercase bytes), `client`, `server`, `root_path`.
5. Embedded **asyncio** runs `app(scope, receive, send)`. `receive()` yields one `http.request` (real body) then `http.disconnect`.
6. `http.response.start` + `http.response.body` are written with `send(..., MSG_NOSIGNAL)`.
7. Keep-alive reuses the connection object from a **pool** (up to 4096).

`GET /metrics` is Prometheus text on the same listener (bind it privately).

---

## Signals

| Signal | Master | Worker |
|--------|--------|--------|
| **SIGTERM / SIGINT** | Stop children, then exit | Leave the event loop after in-flight write when possible |
| **SIGHUP** | Rolling restart (new worker, then SIGTERM old) | — |
| **SIGPIPE** | Ignored | Ignored |

---

## Security defaults

- Caps: request line 8 KiB, headers 64 KiB / 100 fields, body 16 MiB, connections 10 000/worker  
- Duplicate `Content-Length` → **400**  
- `Content-Length` + `chunked` → **400**  
- Integer overflow checks on lengths and chunk size  
- Non-blocking sockets, `MSG_NOSIGNAL`  
- Do not expose `/metrics` on the public internet without a proxy ACL  

---

## Performance (measured, this project)

Recorded on a **2-CPU / ~2 GB** sandbox, localhost, trivial ASGI hello, keep-alive. Not a marketing number.

| Workload | Result |
|----------|--------|
| GET, 1 worker, 32 conns, 100k requests | **~32 700 req/s** |
| GET, 1 worker, 500k requests | **~30 300 req/s** |
| POST 4 KiB body, 20k requests | **~27 100 req/s** (~111 MB/s in) |

The limit is **CPython + one ASGI call per worker**. nginx static will be much faster. A real Django view with a database will be much slower. Add workers on machines with spare cores: `--workers $(nproc)`.

Reproduce:

```bash
./bin/vela examples.hello:application --workers 1 --bind 127.0.0.1:8000
python3 benchmarks/keep_alive_bench.py 127.0.0.1 8000 100000 32
```

---

## Deploy

Nginx → Unix socket → Vela → app. Terminate TLS on nginx.

```nginx
upstream vela { server unix:/run/vela.sock; }
server {
    listen 443 ssl http2;
    ssl_certificate     /etc/ssl/certs/example.pem;
    ssl_certificate_key /etc/ssl/private/example.key;
    location / {
        proxy_pass http://vela;
        proxy_set_header Host $host;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_http_version 1.1;
        proxy_set_header Connection "";
    }
}
```

```bash
vela myproject.asgi:application \
    --workers 4 \
    --unix-socket /run/vela.sock \
    --access-log /var/log/vela/access.log \
    --error-log  /var/log/vela/error.log \
    --pid-file   /run/vela.pid
```

Unit file: [`deploy/vela.service`](deploy/vela.service). More: [`docs/deploy.md`](docs/deploy.md).

---

## Repository

```
vela/
  bin/vela                 # after `make`
  include/vela/            # public C headers
  src/
    core/   event/  http/  network/
    python/ worker/ config/ metrics/ websocket/ cli/
  examples/                # hello, FastAPI-shaped, Django-shaped, live_app
  tests/                   # parser, config, websocket, async + reload mimic
  fuzz/                    # HTTP parser fuzz stub
  benchmarks/
  deploy/                  # systemd + nginx snippet
  docs/                    # architecture, config, operations, name
```

Internal C APIs still use a short `my_*` prefix (loop, buffers, HTTP). The **product, binary, headers path, metrics, and Server header** are **vela**.

---

## What is done vs not

**In and tested:** epoll server, HTTP/1.1 (keep-alive, chunked, bodies), ASGI 3 HTTP, CPython embed, master/worker, Unix + TCP, SIGHUP rolling restart, `--reload`, access/error logs, `/metrics`, smuggling defenses, connection pool.

**Not done (do not pretend):** TLS handshake on the connection, HTTP/2, WebSocket-as-ASGI, lifespan, in-worker concurrent asyncio I/O, native `sendfile` static.

Those are sequential stages, not missing files for show.

---

## Documentation

| | |
|---|---|
| [Architecture](docs/architecture.md) | Event loop, parser, ASGI, memory ownership |
| [Deploy](docs/deploy.md) | systemd, nginx, sockets, privileges |
| [Config](docs/config.md) | Limits and file format |
| [Operations](docs/operations.md) | Logs, metrics, failures |
| [Name](docs/NAME.md) | Why *Vela* |

---

## License

MIT. See [`LICENSE`](LICENSE).

```
vela myproject.asgi:application --workers 4
```

Point the sail. Let nginx face the weather.
