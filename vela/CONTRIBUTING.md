# Contributing to Vela


- C17, `-Wall -Wextra -Wpedantic`. Do not ignore warnings.
- Every syscall checked. No silent `epoll_ctl` failures.
- Parser and config changes need tests under `tests/`.
- Fuzz HTTP via `fuzz/fuzz_http.c` (libFuzzer).
- Do not add HTTP/3 until HTTP/1.1 + ASGI HTTP are solid.
- `--reload` is development-only.

Build: `make test`.
