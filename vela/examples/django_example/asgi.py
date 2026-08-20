"""Minimal Django-style ASGI module.

A full Django project is not vendored. Point Django's ASGI at vela:

    vela myproject.asgi:application --workers 4 --unix-socket /run/vela.sock

This file is a drop-in ASGI 3 app that mimics Django's asgi.py shape.
"""

async def application(scope, receive, send):
    if scope["type"] == "lifespan":
        while True:
            msg = await receive()
            if msg["type"] == "lifespan.startup":
                await send({"type": "lifespan.startup.complete"})
            elif msg["type"] == "lifespan.shutdown":
                await send({"type": "lifespan.shutdown.complete"})
                return
        return
    await receive()
    body = b"django-style ASGI ok\n"
    await send({
        "type": "http.response.start",
        "status": 200,
        "headers": [[b"content-type", b"text/plain"]],
    })
    await send({"type": "http.response.body", "body": body})
