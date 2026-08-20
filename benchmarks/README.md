# Benchmarks

Do not publish numbers without running these against the same hardware.

```
# hello ASGI
./bin/vela examples.hello:application --workers 4 --bind 0.0.0.0:8000

# wrk
wrk -t4 -c128 -d15s http://127.0.0.1:8000/

# Compare
uvicorn examples.hello:application --workers 4 --host 0.0.0.0 --port 8001
```

Record: rps, p50/p95/p99 latency, RSS, CPU. Keep-alive vs close. Large body POST.
