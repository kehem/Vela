# Deploy

Vela belongs **behind nginx** (or another proxy). Terminate TLS there. Talk HTTP/1.1 to Vela on a Unix socket.

## Layout

```
/usr/local/bin/vela          # or vela
/var/www/app/                # Django / FastAPI
/run/vela.sock
/run/vela.pid
/var/log/vela/access.log
/var/log/vela/error.log
```

The socket directory must be writable by the service user. `/tmp/vela.sock` is fine for a first bring-up.

## systemd

See `deploy/vela.service`. A Vela-named unit:

```ini
[Unit]
Description=Vela ASGI server
After=network.target

[Service]
Type=simple
User=www-data
Group=www-data
WorkingDirectory=/var/www/app
ExecStart=/usr/local/bin/vela myproject.asgi:application --workers 4 --unix-socket /run/vela.sock --access-log /var/log/vela/access.log --error-log /var/log/vela/error.log --pid-file /run/vela.pid
ExecReload=/bin/kill -HUP $MAINPID
KillSignal=SIGTERM
TimeoutStopSec=30
Restart=on-failure
LimitNOFILE=65535

[Install]
WantedBy=multi-user.target
```

Drop `--user`/`--group` if systemd already switched identity.

## nginx

```nginx
upstream vela {
    server unix:/run/vela.sock;
}

server {
    listen 443 ssl http2;
    server_name example.com;
    ssl_certificate     /etc/ssl/certs/example.pem;
    ssl_certificate_key /etc/ssl/private/example.key;

    location / {
        proxy_pass http://vela;
        proxy_set_header Host $host;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_http_version 1.1;
        proxy_set_header Connection "";
    }
}
```

## Workers

Start with **one worker per CPU**:

```bash
vela myproject.asgi:application --workers $(nproc) --unix-socket /run/vela.sock
```

Each worker is a full interpreter. Memory ≈ N × (interpreter + app). If the app is heavy, fewer workers and a proxy queue beat swapping.

## Privileges

Bind low ports as root only if you must; prefer the Unix socket + nginx. `--user` / `--group` drop after listen.

## Reload

`systemctl reload vela` → **SIGHUP** → master SIGTERMs children and respawns. In-flight requests on old workers get a graceful window (`graceful_timeout`).
