#include "text.h"

#include <string.h>

/* Overlays 0 (English) and 1 (French) hold all the game's strings: each one is a 2-byte header
 * (always 01 00), glyph codes and control codes, and a 0xEA terminator, followed by a table of
 * 1893 pointers that Text_GetString (02085238) reads through a literal at 0208527c. A translation
 * is laid out the same way, appended to the image, and the FAT, overlay table and literal are
 * pointed at it. */

#define TEXT_RAM 0x021DD280u
#define TEXT_LIMIT (0x021FFDA0u - TEXT_RAM) /* the next overlay slot starts here */
#define TEXT_TABLE_EN 0x021FACC0u
#define TEXT_TABLE_LITERAL 0x0208527Cu
#define STRING_COUNT 1893
#define STRING_MAX 0x4000 /* the longest original is 3920 bytes */
#define CODE_END 0xEA
#define CODE_NEWLINE 0xE6

/* Glyph codes 0x60-0xA9, the Latin-1 letters of the font (0x00-0x5E are ASCII minus 0x20). */
static const Uint16 latin[] = {
    0xA1, 0xA2, 0xA3, 0xA8, 0xA9, 0xAE, 0xB0, 0xB1, 0xB4, 0xB7, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4,
    0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3, 0xD4,
    0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDF, 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5,
    0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5,
    0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0x152, 0x153,
};

static Uint32 rd32(const Uint8 *p)
{
    return (Uint32)p[0] | (Uint32)p[1] << 8 | (Uint32)p[2] << 16 | (Uint32)p[3] << 24;
}

static void wr32(Uint8 *p, Uint32 v)
{
    p[0] = (Uint8)v;
    p[1] = (Uint8)(v >> 8);
    p[2] = (Uint8)(v >> 16);
    p[3] = (Uint8)(v >> 24);
}

static bool arg_code(Uint8 code)
{
    return code == 0xE2 || code == 0xE3 || code == 0xE7 || code == 0xE8 || code == 0xE9;
}

static int glyph(Uint32 cp)
{
    if (cp >= 0x20 && cp <= 0x7E) {
        return (int)cp - 0x20;
    }
    for (int i = 0; i < (int)SDL_arraysize(latin); i++) {
        if (latin[i] == cp) {
            return 0x60 + i;
        }
    }
    switch (cp) {
    case 0x2018: case 0x2019: return 0x07; /* ' */
    case 0x201C: case 0x201D: return 0x02; /* " */
    case 0x2013: case 0x2014: return 0x0D; /* - */
    }
    return -1;
}

static int hex2(const char *s)
{
    int v = 0;
    for (int i = 0; i < 2; i++) {
        char c = s[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) {
            return -1;
        }
        v = v * 16 + d;
    }
    return v;
}

typedef struct Encoder {
    Uint8 buf[STRING_MAX];
    int len;
    int line; /* for error messages */
} Encoder;

static bool put(Encoder *e, int b)
{
    if (e->len >= STRING_MAX) {
        return SDL_SetError("linha %d: texto longo demais", e->line);
    }
    e->buf[e->len++] = (Uint8)b;
    return true;
}

/* Encodes one line of a string body: plain UTF-8 plus {n}, {XX} and {XX:YY} tokens. */
static bool encode_line(Encoder *e, const char *s, const char *end)
{
    while (s < end) {
        if (*s == '{') {
            const char *close = s;
            while (close < end && *close != '}') {
                close++;
            }
            size_t n = (size_t)(close - s - 1);
            if (close < end && n == 1 && s[1] == 'n') {
                if (!put(e, CODE_NEWLINE)) {
                    return false;
                }
            } else if (close < end && (n == 2 || (n == 5 && s[3] == ':')) && hex2(s + 1) >= 0 && (n == 2 || hex2(s + 4) >= 0)) {
                int code = hex2(s + 1);
                if (code == CODE_END) {
                    return SDL_SetError("linha %d: {EA} termina o texto e não pode ser usado", e->line);
                }
                if (arg_code((Uint8)code) != (n == 5)) {
                    return SDL_SetError("linha %d: {%.2s} %s argumento", e->line, s + 1, n == 5 ? "não leva" : "precisa de um");
                }
                if (!put(e, code) || (n == 5 && !put(e, hex2(s + 4)))) {
                    return false;
                }
            } else {
                return SDL_SetError("linha %d: código inválido %.*s", e->line, (int)(close < end ? n + 2 : (size_t)(end - s)), s);
            }
            s = close + 1;
            continue;
        }
        const char *start = s;
        size_t left = (size_t)(end - s);
        Uint32 cp = SDL_StepUTF8(&s, &left);
        int g = glyph(cp);
        if (g < 0) {
            return SDL_SetError("linha %d: a fonte do jogo não tem '%.*s'", e->line, (int)(s - start), start);
        }
        if (!put(e, g)) {
            return false;
        }
    }
    return true;
}

static bool header_line(const char *s, size_t n, int *id)
{
    if (n != 6 || s[0] != '[' || s[5] != ']') {
        return false;
    }
    int v = 0;
    for (int i = 1; i < 5; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
        v = v * 10 + (s[i] - '0');
    }
    *id = v;
    return true;
}

/* Closes the current entry as header + body + terminator. Trailing blank lines were never
 * written: they separate entries, so a string that ends in a line break spells it {n}. */
static bool finish(const Encoder *e, Uint8 **out, int *out_len)
{
    *out = SDL_malloc((size_t)e->len + 3);
    if (!*out) {
        return false;
    }
    (*out)[0] = 0x01;
    (*out)[1] = 0x00;
    SDL_memcpy(*out + 2, e->buf, (size_t)e->len);
    (*out)[e->len + 2] = CODE_END;
    *out_len = e->len + 3;
    return true;
}

static bool parse(const char *path, Uint8 **strings, int *lengths)
{
    size_t size;
    char *data = SDL_LoadFile(path, &size);
    if (!data) {
        return false;
    }
    const char *p = data, *end = data + size;
    if (size >= 3 && SDL_memcmp(p, "\xEF\xBB\xBF", 3) == 0) {
        p += 3;
    }
    bool ok = true;
    int current = -1, line = 0, breaks = 0;
    static Encoder e;
    while (ok && p <= end) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        const char *next = eol ? eol + 1 : end + 1;
        if (!eol) {
            eol = end;
        }
        const char *stop = eol > p && eol[-1] == '\r' ? eol - 1 : eol;
        line++;
        int id;
        if (header_line(p, (size_t)(stop - p), &id)) {
            if (current >= 0) {
                ok = finish(&e, &strings[current], &lengths[current]);
            }
            if (ok && id >= STRING_COUNT) {
                ok = SDL_SetError("linha %d: só existem %d textos (0000-%04d)", line, STRING_COUNT, STRING_COUNT - 1);
            } else if (ok && strings[id]) {
                ok = SDL_SetError("linha %d: [%04d] aparece duas vezes", line, id);
            }
            current = id;
            e.len = 0;
            breaks = -1; /* no line break before the first line */
        } else if (current >= 0) {
            e.line = line;
            breaks++; /* held back: only the breaks that text follows reach the string */
            if (stop > p) {
                for (; ok && breaks > 0; breaks--) {
                    ok = put(&e, CODE_NEWLINE);
                }
                ok = ok && encode_line(&e, p, stop);
            }
        } else if (stop > p && *p != '#') {
            ok = SDL_SetError("linha %d: texto antes do primeiro cabeçalho [NNNN]", line);
        }
        p = next;
    }
    if (ok && current >= 0) {
        ok = finish(&e, &strings[current], &lengths[current]);
    }
    SDL_free(data);
    if (!ok) {
        const char *why = SDL_GetError();
        SDL_SetError("%s: %s", path, why);
    }
    return ok;
}

bool text_apply(NdsRom *rom, const char *path)
{
    Uint32 ovt = rd32(rom->data + 0x50);
    if (rom->overlay_count < 1) {
        return SDL_SetError("a ROM não tem overlays");
    }
    Uint32 file_id = rom->overlays[0].file_id, ov_size;
    const Uint8 *ov = rom_file_by_id(rom, file_id, &ov_size);
    Uint32 literal = rom->arm9_offset + (TEXT_TABLE_LITERAL - rom->arm9_ram);
    if (!ov || rom->overlays[0].ram_addr != TEXT_RAM || literal + 4 > rom->size ||
        rd32(rom->data + literal) != TEXT_TABLE_EN || TEXT_TABLE_EN - TEXT_RAM + STRING_COUNT * 4 > ov_size) {
        return SDL_SetError("o texto em inglês não está onde o port espera (a ROM já foi modificada?)");
    }

    Uint8 *strings[STRING_COUNT] = { 0 };
    int lengths[STRING_COUNT] = { 0 };
    Uint8 *out = NULL;
    bool ok = parse(path, strings, lengths);
    if (ok) {
        out = SDL_calloc(1, TEXT_LIMIT);
        ok = out != NULL;
    }

    /* the layout of the original: strings, a zero static-initializer word, the table, 12 zero bytes */
    Uint32 used = 0, offsets[STRING_COUNT], sizes[STRING_COUNT];
    int translated = 0;
    for (int i = 0; ok && i < STRING_COUNT; i++) {
        const Uint8 *s = strings[i];
        Uint32 len = (Uint32)lengths[i];
        if (s) {
            translated++;
        } else {
            Uint32 o = rd32(ov + (TEXT_TABLE_EN - TEXT_RAM) + i * 4) - TEXT_RAM;
            const Uint8 *t = o < ov_size ? memchr(ov + o + 2, CODE_END, ov_size - o - 2) : NULL;
            if (!t) {
                ok = SDL_SetError("o texto %d da ROM está corrompido", i);
                break;
            }
            s = ov + o;
            len = (Uint32)(t - s) + 1;
        }
        /* identical strings share one copy, which buys back room for longer translations */
        offsets[i] = used;
        sizes[i] = len;
        for (int j = 0; j < i; j++) {
            if (sizes[j] == len && SDL_memcmp(out + offsets[j], s, len) == 0) {
                offsets[i] = offsets[j];
                break;
            }
        }
        if (offsets[i] == used) {
            if (used + len > TEXT_LIMIT) {
                ok = SDL_SetError("a tradução não cabe na memória do jogo; encurte alguns textos (tools/text.py check)");
                break;
            }
            SDL_memcpy(out + used, s, len);
            used += len;
        }
    }
    Uint32 sinit = (used + 3) & ~3u, table = sinit + 4, total = table + STRING_COUNT * 4 + 12;
    if (ok && total > TEXT_LIMIT) {
        ok = SDL_SetError("a tradução passa %u bytes do limite; encurte alguns textos (tools/text.py check)",
                          total - TEXT_LIMIT);
    }
    for (int i = 0; ok && i < STRING_COUNT; i++) {
        wr32(out + table + i * 4, TEXT_RAM + offsets[i]);
    }

    /* the dump is normally padded to the cartridge size, so the new overlay fits after the used area */
    Uint32 place = (rd32(rom->data + 0x80) + 0x1FF) & ~0x1FFu;
    if (ok && place + total > rom->size) {
        Uint8 *grown = SDL_realloc(rom->data, place + total);
        if (!grown) {
            ok = false;
        } else {
            SDL_memset(grown + rom->size, 0xFF, place + total - rom->size);
            rom->data = grown;
            rom->size = place + total;
        }
    }
    if (ok) {
        SDL_memcpy(rom->data + place, out, total);
        Uint8 *fat = rom->data + rom->fat_offset + file_id * 8;
        wr32(fat, place);
        wr32(fat + 4, place + total);
        Uint8 *entry = rom->data + ovt; /* overlay 0 */
        wr32(entry + 8, total);
        wr32(entry + 16, TEXT_RAM + sinit);
        wr32(entry + 20, TEXT_RAM + sinit + 4);
        rom->overlays[0].ram_size = total;
        rom->overlays[0].sinit_start = TEXT_RAM + sinit;
        rom->overlays[0].sinit_end = TEXT_RAM + sinit + 4;
        wr32(rom->data + literal, TEXT_RAM + table);

        Uint8 *h = rom->data;
        wr32(h + 0x80, place + total);
        while (h[0x14] < 15 && (0x20000u << h[0x14]) < place + total) {
            h[0x14]++;
        }
        Uint16 crc = 0xFFFF; /* CRC-16/MODBUS of the header */
        for (int i = 0; i < 0x15E; i++) {
            crc ^= h[i];
            for (int b = 0; b < 8; b++) {
                crc = (crc & 1) ? (Uint16)((crc >> 1) ^ 0xA001) : (Uint16)(crc >> 1);
            }
        }
        h[0x15E] = (Uint8)crc;
        h[0x15F] = (Uint8)(crc >> 8);
        SDL_Log("tradução: %d de %d textos de %s (%u de %u bytes)", translated, STRING_COUNT, path, total, TEXT_LIMIT);
    }

    for (int i = 0; i < STRING_COUNT; i++) {
        SDL_free(strings[i]);
    }
    SDL_free(out);
    return ok;
}
