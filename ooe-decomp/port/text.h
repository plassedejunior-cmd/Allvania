#ifndef OOE_TEXT_H
#define OOE_TEXT_H

#include <stdbool.h>

#include "rom.h"

/* Replaces the English text (overlay 0) with the strings of a translation file, in memory.
 * Strings the file leaves out keep the original text. The format is in docs/game/text.md and
 * matches tools/text.py; on failure the ROM is untouched and SDL_GetError says why. */
bool text_apply(NdsRom *rom, const char *path);

#endif
