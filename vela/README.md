# Vela

**A native C ASGI 3 server.**  
Short for *velum* — a sail. Light, taut, pointed at the wind.

```
vela myproject.asgi:application --workers 4 --bind 0.0.0.0:8000
```

The command is **`vela`**.

```
Internet → Nginx → unix:/run/vela.sock → Vela workers → CPython → Django / FastAPI
```

Linux epoll. Embedded CPython. Process isolation. HTTP/1.1. Unix sockets. Master/worker. Built to sit behind a reverse proxy the way nginx sits in front of one.

---

## Why Vela

| | |
|---|---|
| **Native** | C17 event loop, not a Python asyncio server wrapping sockets |
| **ASGI 3** | Real `scope` / `receive` / `send` for Django and FastAPI |
| **Isolated** | `fork` workers, not threads pretending to be cores |
| **Honest** | Limits, smuggling defenses, 500s that do not kill the worker |

---

## Install

```bash
make
sudo install -m 755 bin/vela /usr/local/bin/vela
```

Needs: GCC (C17), Linux, CPython 3.11+ headers, OpenSSL, pthread.

```bash
cmake -B build && cmake --build build && ctest --test-dir build   # if CMake is present
make test
```

---

## Serve

```bash
# TCP
vela examples.hello:application --bind 127.0.0.1:8000

# Unix socket (nginx)
vela myproject.asgi:application --unix-socket /run/vela.sock --workers 4

# Production-shaped
vela myproject.asgi:application \
    --workers 4 \
    --unix-socket /run/vela.sock \
    --access-log /var/log/vela/access.log \
    --error-log  /var/log/vela/error.log \
    --pid-file   /run/vela.pid
```

Nginx:

```nginx
upstream vela { server unix:/run/vela.sock; }
server {
    listen 80;
    location / { proxy_pass http://vela; proxy_http_version 1.1; }
}
```

---

## Flags

| Flag | Meaning |
|------|---------|
| `--bind host:port` | TCP listen (repeatable) |
| `--unix-socket PATH` | Unix socket file |
| `--workers N` | Isolated processes (`SO_REUSEPORT`) |
| `--log-level` | debug / info / warning / error / critical |
| `--access-log` / `--error-log` | `-` for stdout |
| `--timeout` / `--keep-alive` | seconds |
| `--max-connections` / `--max-request-size` | caps |
| `--python-path` / `--working-directory` | app discovery |
| `--config FILE` | `key = value` file |
| `--pid-file` / `--user` / `--group` / `--daemon` | service |
| `--help` / `--version` | |

CLI overrides the config file.

---

## Signals

| Signal | Master |
|--------|--------|
| **SIGTERM / SIGINT** | Drain and stop |
| **SIGHUP** | Rolling worker restart |
| **SIGPIPE** | Ignored |

Workers finish in-flight responses before exit when they can.

---

## Docs

| | |
|---|---|
| [Architecture](docs/architecture.md) | Event loop, HTTP, ASGI, memory |
| [Deploy](docs/deploy.md) | systemd, nginx, sockets, privileges |
| [Config](docs/config.md) | File format and limits |
| [Operations](docs/operations.md) | Logs, metrics, troubleshooting |

License: MIT. See `LICENSE`.
