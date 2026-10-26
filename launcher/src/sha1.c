#include "sha1.h"

#include <SDL3/SDL.h>

typedef struct Sha1 {
    Uint32 h[5];
    Uint64 length;
    Uint8 block[64];
    size_t used;
} Sha1;

static Uint32 rol(Uint32 v, int n)
{
    return (v << n) | (v >> (32 - n));
}

static void compress(Sha1 *s, const Uint8 *p)
{
    Uint32 w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = (Uint32)p[i * 4] << 24 | (Uint32)p[i * 4 + 1] << 16 | (Uint32)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    }
    for (int i = 16; i < 80; i++) {
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    Uint32 a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for (int i = 0; i < 80; i++) {
        Uint32 f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        Uint32 t = rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol(b, 30);
        b = a;
        a = t;
    }
    s->h[0] += a;
    s->h[1] += b;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
}

static void update(Sha1 *s, const Uint8 *data, size_t len)
{
    s->length += len;
    while (len) {
        size_t n = SDL_min(len, 64 - s->used);
        SDL_memcpy(s->block + s->used, data, n);
        s->used += n;
        data += n;
        len -= n;
        if (s->used == 64) {
            compress(s, s->block);
            s->used = 0;
        }
    }
}

bool sha1_file(const char *path, char out[41])
{
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (!io) {
        return false;
    }
    Sha1 s = { { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 }, 0, { 0 }, 0 };
    static Uint8 buf[1 << 16];
    size_t n;
    while ((n = SDL_ReadIO(io, buf, sizeof(buf))) > 0) {
        update(&s, buf, n);
    }
    SDL_CloseIO(io);

    Uint64 bits = s.length * 8;
    Uint8 pad = 0x80;
    update(&s, &pad, 1);
    pad = 0;
    while (s.used != 56) {
        update(&s, &pad, 1);
    }
    Uint8 len[8];
    for (int i = 0; i < 8; i++) {
        len[i] = (Uint8)(bits >> (56 - 8 * i));
    }
    update(&s, len, 8);

    for (int i = 0; i < 5; i++) {
        SDL_snprintf(out + i * 8, 9, "%08x", s.h[i]);
    }
    return true;
}
