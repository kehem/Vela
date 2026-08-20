import json
MARKER = 'v2'

async def application(scope, receive, send):
    msg = await receive()
    path = scope.get('path') or '/'
    n = len(msg.get('body') or b'')
    payload = json.dumps({'ok': True, 'marker': MARKER, 'path': path, 'n': n}) + '\n'
    b = payload.encode()
    await send({
        'type': 'http.response.start',
        'status': 200,
        'headers': [
            [b'content-type', b'application/json'],
            [b'content-length', str(len(b)).encode()],
        ],
    })
    await send({'type': 'http.response.body', 'body': b})
