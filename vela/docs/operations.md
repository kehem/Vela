# Operations

## Logs

Access (when `--access-log` is set):

```
127.0.0.1 - GET /api/users HTTP/1.1 200 0 4.2ms
```

Error log: worker lifecycle, bind failures, Python tracebacks. Tracebacks from the app are **not** a worker crash.

Levels: `debug`, `info`, `warning`, `error`, `critical`.

Leave access logs off on the hottest path if you are chasing RPS; they serialize on a mutex.

## Health

- Process: master PID file, workers visible in `ps`
- HTTP: `GET /` through nginx
- `GET /metrics` from a trusted network

A 500 with a traceback in the error log is the app. A silent accept/bind error is the server.

## Troubleshooting

| Symptom | Check |
|---------|--------|
| `ASGI target must be module:attribute` | Use `pkg.asgi:application`, not a file path |
| import errors | `--python-path`, `--working-directory`, `PYTHONPATH` |
| `Address already in use` | another listener; Unix: stale sock should be unlinked — confirm path |
| nginx 502 | socket path, permissions (`0660`), matching user/group |
| POST body empty in the app | upgrade: bodies are in `req.body` as of the ASGI runner fix |
| Worker restart loop | app fails at import; run one worker in the foreground |
| Slow RPS | add `--workers`; this is CPython ASGI, not static nginx |

## Benchmarks

Reproducible, not marketing:

```bash
vela examples.hello:application --workers $(nproc) --bind 127.0.0.1:8000
python3 benchmarks/keep_alive_bench.py 127.0.0.1 8000 100000 32
```

Compare on the same box against Uvicorn/Hypercorn. Publish p50/p95/p99 and RSS, not a single RPS number.

The process and binary are both **vela**.
