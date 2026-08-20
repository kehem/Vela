async def application(scope, receive, send):
    if scope["type"] != "http":
        return
    await receive()
    body = b"hello from vela ASGI\n"
    await send(
        {
            "type": "http.response.start",
            "status": 200,
            "headers": [
                [b"content-type", b"text/plain; charset=utf-8"],
                [b"content-length", str(len(body)).encode()],
            ],
        }
    )
    await send({"type": "http.response.body", "body": body})
