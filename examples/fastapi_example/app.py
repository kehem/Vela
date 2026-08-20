"""FastAPI example. Run: vela examples.fastapi_example.app:app --bind 127.0.0.1:8000

Requires: pip install fastapi
"""

try:
    from fastapi import FastAPI

    app = FastAPI(title="vela FastAPI example")

    @app.get("/")
    async def root():
        return {"server": "vela", "framework": "fastapi"}

    @app.get("/items/{item_id}")
    async def read_item(item_id: int):
        return {"item_id": item_id}

except ImportError:
    async def app(scope, receive, send):
        await receive()
        body = b'{"error":"install fastapi"}\n'
        await send({"type": "http.response.start", "status": 500,
                    "headers": [[b"content-type", b"application/json"]]})
        await send({"type": "http.response.body", "body": body})
