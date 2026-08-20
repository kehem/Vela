# Config

CLI wins over file.

```
vela myproject.asgi:application --config /etc/vela.conf --workers 8
```

## File

Simple `key = value` (TOML-ish, no nested tables required):

```
workers = 4
bind = "0.0.0.0:8000"
unix_socket = "/run/vela.sock"
keep_alive = 5
request_timeout = 60
max_connections = 10000
log_level = "info"
```

`#` comments and `[sections]` lines are ignored.

## Limits (defaults)

| Key | Default | On exceed |
|-----|---------|-----------|
| request line | 8 KiB | 414 |
| headers | 64 KiB, 100 fields | 431 |
| body | 16 MiB | 413 |
| connections / worker | 10 000 | accept dropped |
| keep-alive | 5 s | (tick; tighten in later builds) |
| request timeout | 60 s | |

Duplicate `Content-Length` or mixing it with `chunked` is **400** (request smuggling).

## App target

`module.submodule:attribute`

Examples:

```
myproject.asgi:application
examples.hello:application
examples.fastapi_example.app:app
```

`--python-path` is prepended to `sys.path`. `--working-directory` `chdir`s first.

## Metrics

`GET /metrics` on the same port is Prometheus text:

```
vela_requests_total
vela_errors_total
vela_responses{code="2xx"|3xx|4xx|5xx}
vela_active_connections
vela_request_latency_avg_us
```

Point a scraper at the proxy only if you intend it to be public; otherwise bind Vela to localhost / Unix socket.
