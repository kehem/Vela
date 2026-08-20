# Architecture

Vela is a **master + workers** native server. The master does not parse HTTP. Workers own sockets, the parser, and one embedded CPython interpreter each.

```
                    ┌──────────── master ────────────┐
                    │  bind / fork / waitpid / SIGHUP │
                    └──────┬──────────┬──────────┬────┘
                           │          │          │
                      worker 1   worker 2   worker N
                           │
              epoll ── HTTP/1.1 ── ASGI 3 ── CPython
```

## Event loop

`src/event/loop.c` wraps Linux `epoll`. The public API is not epoll-shaped:

```
my_loop_create / add / mod / del / run / stop
```

Client sockets use **edge-triggered** I/O. Listen sockets stay **level-triggered** so accepts are not lost. A self-pipe wakes `stop`. A tick callback is the place for idle timeouts.

`EAGAIN`, `EINTR`, partial reads/writes, and hangups are handled in the connection state machine (`src/network/conn.c`).

## HTTP/1.1

`src/http/parser.c` is a byte state machine. No `sscanf` on the wire.

- Methods and targets copied into reusable buffers
- Duplicate `Content-Length` → 400
- `Content-Length` + `chunked` → 400
- Overflow-checked decimal and chunk sizes
- Assembled body in `req.body` (reused on keep-alive) for ASGI `http.request`

Keep-alive reuses the connection object and its buffers.

## ASGI 3

Each worker:

1. `Py_InitializeFromConfig`
2. Loads `module:attribute`
3. For a complete request, builds `scope` (bytes headers, `raw_path`, `query_string`)
4. Runs `app(scope, receive, send)` on a private `asyncio` loop
5. Writes `http.response.start` + body to the socket buffer

Application exceptions are logged; the worker returns **500** and stays up.

**Trade-off:** one ASGI call at a time per worker. Concurrency is **processes**, like a strict gunicorn-prefork, not uvloop-in-one-process. That keeps the C loop simple and crash domains small.

## Memory

| Object | Owner |
|--------|--------|
| `my_conn_t` | worker; returned to a pool on close |
| `in` / `out` buffers | the connection |
| `req.store` / `req.body` / `req.target` | the request, **kept across keep-alive** |
| `PyObject *` | CPython; GIL held for the ASGI call |

Hot path avoids free/malloc of connections when the pool has spare objects (cap 4096).

## Listen path

TCP: `SO_REUSEADDR`, `SO_REUSEPORT`, `TCP_NODELAY` on clients, optional `TCP_DEFER_ACCEPT` / `TCP_FASTOPEN`, large listen backlog.

Unix: unlink stale file, `bind`, `chmod`, unlink on shutdown.

## What Vela is not (yet)

TLS on the connection, HTTP/2, HTTP/3, lifespan, WebSocket-as-ASGI, in-process `--reload`. The parsers and layout leave room for those without rewriting HTTP/1.1.
