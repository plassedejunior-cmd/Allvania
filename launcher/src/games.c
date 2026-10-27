#include "games.h"

static bool file_exists(const char *path)
{
    SDL_PathInfo info;
    return path && SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}

static SDL_Color parse_color(const char *hex)
{
    SDL_Color c = { 40, 10, 20, 255 };
    if (hex && hex[0] == '#' && SDL_strlen(hex) == 7) {
        Uint32 v = (Uint32)SDL_strtoul(hex + 1, NULL, 16);
        c.r = (Uint8)(v >> 16);
        c.g = (Uint8)(v >> 8);
        c.b = (Uint8)v;
    }
    return c;
}

/* The exe in games.ini is relative to the manifest's folder; Windows builds add ".exe". */
static char *resolve_exe(const Library *lib, const char *rel)
{
    if (!rel || !*rel) {
        return NULL;
    }
    char *path;
    SDL_asprintf(&path, "%s%s", lib->manifest_dir, rel);
    if (file_exists(path)) {
        return path;
    }
    char *with_ext;
    SDL_asprintf(&with_ext, "%s.exe", path);
    SDL_free(path);
    if (file_exists(with_ext)) {
        return with_ext;
    }
    SDL_free(with_ext);
    return NULL;
}

/* The optional translation (text=) is relative to the manifest too, and skipped until it exists. */
static char *resolve_text(const Library *lib, const char *rel)
{
    if (!rel || !*rel) {
        return NULL;
    }
    char *path;
    SDL_asprintf(&path, "%s%s", lib->manifest_dir, rel);
    if (file_exists(path)) {
        return path;
    }
    SDL_free(path);
    return NULL;
}

static void add_game(const char *section, void *user)
{
    Library *lib = user;
    if (lib->count == MAX_GAMES) {
        return;
    }
    const Ini *m = &lib->manifest;
    int i = lib->count++;
    Game *g = &lib->games[i];
    g->id = section;
    g->series = ini_get(m, section, "series", "Castlevania");
    g->title = ini_get(m, section, "title", section);
    g->platform = ini_get(m, section, "platform", "");
    g->year = ini_get(m, section, "year", "");
    g->description = ini_get(m, section, "description", "");
    g->rom_sha1 = ini_get(m, section, "rom_sha1", NULL);
    g->status = SDL_strcmp(ini_get(m, section, "status", "soon"), "available") == 0 ? GAME_AVAILABLE : GAME_COMING_SOON;
    g->dual_screen = ini_get_int(m, section, "screens", 1) == 2;
    g->accent[0] = parse_color(ini_get(m, section, "color1", NULL));
    g->accent[1] = parse_color(ini_get(m, section, "color2", NULL));
    lib->exe_paths[i] = resolve_exe(lib, ini_get(m, section, "exe", NULL));
    g->exe = lib->exe_paths[i];
    lib->text_paths[i] = resolve_text(lib, ini_get(m, section, "text", NULL));
    g->text = lib->text_paths[i];
}

bool library_load(Library *lib)
{
    SDL_zerop(lib);
    const char *base = SDL_GetBasePath();
    char *manifest_path;
    SDL_asprintf(&manifest_path, "%sgames.ini", base);
    bool ok = ini_load(&lib->manifest, manifest_path);
    SDL_free(manifest_path);
    if (!ok) {
        SDL_Log("games.ini not found next to the launcher (%s)", base);
        return false;
    }
    lib->manifest_dir = SDL_strdup(base);

    char *pref = SDL_GetPrefPath("CastlevaniaCollection", "launcher");
    SDL_asprintf(&lib->config_path, "%slauncher.ini", pref ? pref : base);
    SDL_free(pref);
    ini_load(&lib->config, lib->config_path);

    ini_each_section(&lib->manifest, add_game, lib);
    return true;
}

void library_free(Library *lib)
{
    for (int i = 0; i < lib->count; i++) {
        SDL_free(lib->exe_paths[i]);
        SDL_free(lib->text_paths[i]);
    }
    ini_free(&lib->manifest);
    ini_free(&lib->config);
    SDL_free(lib->config_path);
    SDL_free(lib->manifest_dir);
}

void library_save_config(Library *lib)
{
    if (!ini_save(&lib->config, lib->config_path)) {
        SDL_Log("could not write %s: %s", lib->config_path, SDL_GetError());
    }
}

const char *library_rom_path(const Library *lib, const Game *game)
{
    return ini_get(&lib->config, "roms", game->id, NULL);
}

bool library_rom_verified(const Library *lib, const Game *game)
{
    return ini_get_int(&lib->config, "rom_verified", game->id, 0) == 1 && file_exists(library_rom_path(lib, game));
}

void library_set_rom(Library *lib, const Game *game, const char *path, bool verified)
{
    ini_set(&lib->config, "roms", game->id, path);
    ini_set(&lib->config, "rom_verified", game->id, verified ? "1" : "0");
    library_save_config(lib);
}

bool library_split_screens(const Library *lib, const Game *game)
{
    return game->dual_screen && SDL_strcmp(ini_get(&lib->config, "screen_layout", game->id, "original"), "split") == 0;
}

void library_set_split_screens(Library *lib, const Game *game, bool split)
{
    ini_set(&lib->config, "screen_layout", game->id, split ? "split" : "original");
    library_save_config(lib);
}

const char *library_save_root(const Library *lib)
{
    const char *custom = ini_get(&lib->config, "launcher", "save_dir", NULL);
    if (custom && *custom) {
        return custom;
    }
    static char *local;
    if (!local) {
        char *pref = SDL_GetPrefPath("CastlevaniaCollection", "saves");
        local = pref ? pref : SDL_strdup(".");
    }
    return local;
}

void library_set_save_root(Library *lib, const char *path)
{
    ini_set(&lib->config, "launcher", "save_dir", path);
    library_save_config(lib);
}

bool library_save_root_is_custom(const Library *lib)
{
    const char *custom = ini_get(&lib->config, "launcher", "save_dir", NULL);
    return custom && *custom;
}

char *library_detect_drive_folder(void)
{
#if defined(SDL_PLATFORM_WINDOWS)
    /* Google Drive for desktop mounts a virtual drive, G: by default */
    static const char *candidates[] = { "G:\\My Drive", "G:\\Meu Drive" };
    for (size_t i = 0; i < SDL_arraysize(candidates); i++) {
        SDL_PathInfo info;
        if (SDL_GetPathInfo(candidates[i], &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
            return SDL_strdup(candidates[i]);
        }
    }
    return NULL;
#else
    /* macOS: ~/Library/CloudStorage/GoogleDrive-<account>/My Drive */
    const char *home = SDL_GetUserFolder(SDL_FOLDER_HOME);
    if (!home) {
        return NULL;
    }
    char *storage;
    SDL_asprintf(&storage, "%sLibrary/CloudStorage", home);
    int n = 0;
    char **found = SDL_GlobDirectory(storage, "GoogleDrive-*", 0, &n);
    char *result = NULL;
    for (int i = 0; i < n && !result; i++) {
        static const char *drives[] = { "My Drive", "Meu Drive" };
        for (size_t d = 0; d < SDL_arraysize(drives) && !result; d++) {
            char *path;
            SDL_asprintf(&path, "%s/%s/%s", storage, found[i], drives[d]);
            SDL_PathInfo info;
            if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
                result = path;
            } else {
                SDL_free(path);
            }
        }
    }
    SDL_free(found);
    SDL_free(storage);
    return result;
#endif
}
