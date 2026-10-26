#include "rom.h"

static Uint32 rd32(const Uint8 *p)
{
    return (Uint32)p[0] | (Uint32)p[1] << 8 | (Uint32)p[2] << 16 | (Uint32)p[3] << 24;
}

static Uint16 rd16(const Uint8 *p)
{
    return (Uint16)(p[0] | p[1] << 8);
}

static bool in_bounds(const NdsRom *rom, Uint32 offset, Uint32 size)
{
    return offset <= rom->size && size <= rom->size - offset;
}

bool rom_load(NdsRom *rom, const char *path)
{
    SDL_zerop(rom);
    rom->data = SDL_LoadFile(path, &rom->size);
    if (!rom->data) {
        return false;
    }
    if (rom->size < 0x4000) {
        SDL_SetError("%s is too small to be a DS ROM", path);
        rom_free(rom);
        return false;
    }
    const Uint8 *h = rom->data;
    SDL_memcpy(rom->title, h, 12);
    SDL_memcpy(rom->game_code, h + 0x0C, 4);
    rom->arm9_offset = rd32(h + 0x20);
    rom->arm9_entry = rd32(h + 0x24);
    rom->arm9_ram = rd32(h + 0x28);
    rom->arm9_size = rd32(h + 0x2C);
    rom->arm7_offset = rd32(h + 0x30);
    rom->arm7_entry = rd32(h + 0x34);
    rom->arm7_ram = rd32(h + 0x38);
    rom->arm7_size = rd32(h + 0x3C);
    rom->fnt_offset = rd32(h + 0x40);
    rom->fnt_size = rd32(h + 0x44);
    rom->fat_offset = rd32(h + 0x48);
    rom->fat_size = rd32(h + 0x4C);
    rom->banner_offset = rd32(h + 0x68);

    Uint32 ovt_offset = rd32(h + 0x50), ovt_size = rd32(h + 0x54);
    if (!in_bounds(rom, rom->fat_offset, rom->fat_size) || !in_bounds(rom, rom->fnt_offset, rom->fnt_size) ||
        !in_bounds(rom, ovt_offset, ovt_size)) {
        SDL_SetError("%s has a corrupt header", path);
        rom_free(rom);
        return false;
    }
    rom->overlay_count = (int)(ovt_size / 32);
    rom->overlays = SDL_calloc(rom->overlay_count ? rom->overlay_count : 1, sizeof(NdsOverlay));
    for (int i = 0; i < rom->overlay_count; i++) {
        const Uint8 *e = rom->data + ovt_offset + i * 32;
        rom->overlays[i] = (NdsOverlay){ rd32(e), rd32(e + 4), rd32(e + 8), rd32(e + 12),
                                         rd32(e + 16), rd32(e + 20), rd32(e + 24) };
    }
    return true;
}

void rom_free(NdsRom *rom)
{
    SDL_free(rom->data);
    SDL_free(rom->overlays);
    SDL_zerop(rom);
}

const Uint8 *rom_file_by_id(const NdsRom *rom, Uint32 id, Uint32 *size)
{
    if (id >= rom->fat_size / 8) {
        return NULL;
    }
    const Uint8 *e = rom->data + rom->fat_offset + id * 8;
    Uint32 start = rd32(e), end = rd32(e + 4);
    if (end < start || !in_bounds(rom, start, end - start)) {
        return NULL;
    }
    if (size) {
        *size = end - start;
    }
    return rom->data + start;
}

int rom_path_to_id(const NdsRom *rom, const char *path)
{
    const Uint8 *fnt = rom->data + rom->fnt_offset;
    Uint32 dir = 0; /* index into the main directory table (root = 0xF000) */
    while (*path == '/') {
        path++;
    }
    while (*path) {
        const char *slash = SDL_strchr(path, '/');
        size_t len = slash ? (size_t)(slash - path) : SDL_strlen(path);
        Uint32 pos = rd32(fnt + dir * 8);
        Uint32 file_id = rd16(fnt + dir * 8 + 4);
        bool found = false;
        while (pos < rom->fnt_size) {
            Uint8 tl = fnt[pos++];
            if (tl == 0) {
                break;
            }
            Uint8 name_len = tl & 0x7F;
            bool is_dir = tl & 0x80;
            bool match = name_len == len && SDL_strncasecmp((const char *)fnt + pos, path, len) == 0;
            pos += name_len;
            if (is_dir) {
                Uint16 child = rd16(fnt + pos);
                pos += 2;
                if (match && slash) {
                    dir = child & 0x0FFF;
                    found = true;
                    break;
                }
            } else {
                if (match && !slash) {
                    return (int)file_id;
                }
                file_id++;
            }
        }
        if (!found) {
            return -1;
        }
        path = slash + 1;
    }
    return -1;
}

const Uint8 *rom_file(const NdsRom *rom, const char *path, Uint32 *size)
{
    int id = rom_path_to_id(rom, path);
    return id < 0 ? NULL : rom_file_by_id(rom, (Uint32)id, size);
}

bool rom_banner_icon(const NdsRom *rom, Uint32 out_rgba[32 * 32])
{
    if (!rom->banner_offset || !in_bounds(rom, rom->banner_offset, 0x240)) {
        return false;
    }
    const Uint8 *bitmap = rom->data + rom->banner_offset + 0x20; /* 4bpp, 4x4 tiles of 8x8 */
    const Uint8 *palette = rom->data + rom->banner_offset + 0x220;
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            int tile = (y / 8) * 4 + x / 8;
            int px = (y % 8) * 8 + x % 8;
            Uint8 b = bitmap[tile * 32 + px / 2];
            int index = (px & 1) ? b >> 4 : b & 0xF;
            Uint16 c = rd16(palette + index * 2);
            Uint8 r = (Uint8)((c & 0x1F) * 255 / 31), g = (Uint8)(((c >> 5) & 0x1F) * 255 / 31),
                  bl = (Uint8)(((c >> 10) & 0x1F) * 255 / 31);
            out_rgba[y * 32 + x] = index == 0 ? 0 : (Uint32)r << 24 | (Uint32)g << 16 | (Uint32)bl << 8 | 0xFF;
        }
    }
    return true;
}
