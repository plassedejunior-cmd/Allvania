#ifndef LAUNCHER_GAMES_H
#define LAUNCHER_GAMES_H

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "ini.h"

#define MAX_GAMES 32

typedef enum GameStatus {
    GAME_AVAILABLE,
    GAME_COMING_SOON,
} GameStatus;

typedef struct Game {
    const char *id;
    const char *series;
    const char *title;
    const char *platform;
    const char *year;
    const char *description;
    const char *exe;      /* resolved absolute path of the game's executable, NULL when not built */
    const char *rom_sha1; /* expected SHA1 of the user's dump */
    GameStatus status;
    SDL_Color accent[2]; /* fallback art gradient */
} Game;

typedef struct Library {
    Ini manifest; /* games.ini shipped with the launcher, read-only */
    Ini config;   /* launcher.ini in the user's pref dir: save folder and ROM paths */
    char *config_path;
    char *manifest_dir;
    Game games[MAX_GAMES];
    char *exe_paths[MAX_GAMES];
    int count;
} Library;

bool library_load(Library *lib);
void library_free(Library *lib);
void library_save_config(Library *lib);

const char *library_rom_path(const Library *lib, const Game *game);
bool library_rom_verified(const Library *lib, const Game *game);
void library_set_rom(Library *lib, const Game *game, const char *path, bool verified);

/* Folder that holds every game's saves, e.g. a Google Drive synced folder. Never NULL. */
const char *library_save_root(const Library *lib);
void library_set_save_root(Library *lib, const char *path);
bool library_save_root_is_custom(const Library *lib);

/* Best guess for the Google Drive for desktop folder, or NULL. Caller frees. */
char *library_detect_drive_folder(void);

#endif
