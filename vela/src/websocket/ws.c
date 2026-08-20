#include "vela/ws.h"

#include <openssl/sha.h>
#include <openssl/evp.h>
#include <string.h>
#include <stdio.h>

int my_ws_parse_frame(const uint8_t *data, size_t len, int *fin, int *opcode, uint8_t **payload,
                      size_t *plen, size_t *consumed)
{
    if (len < 2)
        return 0;
    *fin = (data[0] >> 7) & 1;
    *opcode = data[0] & 0x0f;
    int masked = (data[1] >> 7) & 1;
    uint64_t l = data[1] & 0x7f;
    size_t off = 2;
    if (l == 126) {
        if (len < 4)
            return 0;
        l = ((uint64_t)data[2] << 8) | data[3];
        off = 4;
    } else if (l == 127) {
        if (len < 10)
            return 0;
        l = 0;
        for (int i = 0; i < 8; i++)
            l = (l << 8) | data[2 + i];
        off = 10;
        if (l & (1ull << 63))
            return -1;
    }
    uint8_t mask[4] = {0};
    if (masked) {
        if (len < off + 4)
            return 0;
        memcpy(mask, data + off, 4);
        off += 4;
    }
    if (len < off + l)
        return 0;
    uint8_t *out = (uint8_t *)(data + off);
    if (masked) {
        for (uint64_t i = 0; i < l; i++)
            out[i] ^= mask[i & 3];
    }
    *payload = out;
    *plen = (size_t)l;
    *consumed = off + (size_t)l;
    return 1;
}

int my_ws_build_frame(uint8_t *out, size_t cap, int fin, int opcode, const uint8_t *payload,
                      size_t plen, size_t *written)
{
    size_t need = 2 + plen;
    if (plen >= 126 && plen <= 65535)
        need += 2;
    else if (plen > 65535)
        need += 8;
    if (cap < need)
        return -1;
    size_t off = 0;
    out[off++] = (uint8_t)((fin ? 0x80 : 0) | (opcode & 0x0f));
    if (plen < 126) {
        out[off++] = (uint8_t)plen;
    } else if (plen <= 65535) {
        out[off++] = 126;
        out[off++] = (uint8_t)(plen >> 8);
        out[off++] = (uint8_t)plen;
    } else {
        out[off++] = 127;
        for (int i = 7; i >= 0; i--)
            out[off++] = (uint8_t)((plen >> (i * 8)) & 0xff);
    }
    if (payload && plen)
        memcpy(out + off, payload, plen);
    *written = off + plen;
    return 0;
}

static const char *ws_magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

int my_ws_check_upgrade(const char *upgrade, const char *connection, const char *key,
                        char accept_out[32])
{
    (void)accept_out;
    if (!upgrade || !connection || !key)
        return 0;
    /* case-insensitive substring websocket / upgrade */
    int up = 0, conn = 0;
    for (const char *p = upgrade; *p; p++) {
        if ((p[0] == 'w' || p[0] == 'W') && strncasecmp(p, "websocket", 9) == 0)
            up = 1;
    }
    for (const char *p = connection; *p; p++) {
        if ((p[0] == 'u' || p[0] == 'U') && strncasecmp(p, "upgrade", 7) == 0)
            conn = 1;
    }
    if (!up || !conn)
        return 0;
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hlen = 0;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx)
        return 0;
    if (EVP_DigestInit_ex(ctx, EVP_sha1(), NULL) != 1 ||
        EVP_DigestUpdate(ctx, key, strlen(key)) != 1 ||
        EVP_DigestUpdate(ctx, ws_magic, strlen(ws_magic)) != 1 ||
        EVP_DigestFinal_ex(ctx, hash, &hlen) != 1) {
        EVP_MD_CTX_free(ctx);
        return 0;
    }
    EVP_MD_CTX_free(ctx);
    if (hlen != 20)
        return 0;
    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int j = 0;
    unsigned char t[20];
    memcpy(t, hash, 20);
    for (int i = 0; i < 18; i += 3) {
        accept_out[j++] = b64[t[i] >> 2];
        accept_out[j++] = b64[((t[i] & 3) << 4) | (t[i + 1] >> 4)];
        accept_out[j++] = b64[((t[i + 1] & 15) << 2) | (t[i + 2] >> 6)];
        accept_out[j++] = b64[t[i + 2] & 63];
    }
    accept_out[j++] = b64[t[18] >> 2];
    accept_out[j++] = b64[((t[18] & 3) << 4) | (t[19] >> 4)];
    accept_out[j++] = b64[(t[19] & 15) << 2];
    accept_out[j++] = '=';
    accept_out[j] = 0;
    return 1;
}
