"""ASGI app used for async integration tests."""
import asyncio
import json


async def application(scope, receive, send):
    if scope["type"] != "http":
        return
    msg = await receive()
    path = scope.get("path") or "/"
    method = scope.get("method") or "GET"

    if path == "/sleep":
        await asyncio.sleep(0.05)
        payload = {"ok": True, "path": path, "slept_ms": 50}
    elif path == "/echo":
        body = msg.get("body") or b""
        payload = {"ok": True, "path": path, "method": method, "n": len(body)}
    elif path == "/error":
        raise RuntimeError("async_test boom")
    else:
        payload = {"ok": True, "path": path, "method": method}

    body = (json.dumps(payload) + "\n").encode()
    await send(
        {
            "type": "http.response.start",
            "status": 200,
            "headers": [
                [b"content-type", b"application/json"],
                [b"content-length", str(len(body)).encode()],
            ],
        }
    )
    await send({"type": "http.response.body", "body": body})
