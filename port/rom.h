#ifndef OOE_ROM_H
#define OOE_ROM_H

#include <stdbool.h>

#include <SDL3/SDL.h>

/* Read-only view of the user's .nds dump. Everything the port needs (ARM9, overlays,
 * data files) is read straight from the image; nothing is extracted to disk. */

typedef struct NdsOverlay {
    Uint32 id;
    Uint32 ram_addr;
    Uint32 ram_size;
    Uint32 bss_size;
    Uint32 sinit_start;
    Uint32 sinit_end;
    Uint32 file_id;
} NdsOverlay;

typedef struct NdsRom {
    Uint8 *data;
    size_t size;

    char title[13];
    char game_code[5];
    Uint32 arm9_offset, arm9_entry, arm9_ram, arm9_size;
    Uint32 arm7_offset, arm7_entry, arm7_ram, arm7_size;
    Uint32 fnt_offset, fnt_size, fat_offset, fat_size;
    Uint32 banner_offset;

    NdsOverlay *overlays;
    int overlay_count;
} NdsRom;

bool rom_load(NdsRom *rom, const char *path);
void rom_free(NdsRom *rom);

/* FAT access by file id; returns NULL for an invalid id. */
const Uint8 *rom_file_by_id(const NdsRom *rom, Uint32 id, Uint32 *size);
/* Looks up a path such as "/sc/f_konami0.dat" (leading slash optional) in the file name table. */
const Uint8 *rom_file(const NdsRom *rom, const char *path, Uint32 *size);
/* Converts a path to its FAT id, or -1. */
int rom_path_to_id(const NdsRom *rom, const char *path);

/* Decodes the 32x32 banner icon to RGBA8888 (1024 pixels). */
bool rom_banner_icon(const NdsRom *rom, Uint32 out_rgba[32 * 32]);

#endif
