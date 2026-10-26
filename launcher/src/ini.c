#include "ini.h"

#include <SDL3/SDL.h>

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    char *end = s + SDL_strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        *--end = '\0';
    }
    return s;
}

static void push(Ini *ini, const char *section, const char *key, const char *value)
{
    if (ini->count == ini->capacity) {
        ini->capacity = ini->capacity ? ini->capacity * 2 : 32;
        ini->entries = SDL_realloc(ini->entries, sizeof(IniEntry) * ini->capacity);
    }
    IniEntry *e = &ini->entries[ini->count++];
    e->section = SDL_strdup(section);
    e->key = SDL_strdup(key);
    e->value = SDL_strdup(value);
}

bool ini_load(Ini *ini, const char *path)
{
    size_t size;
    char *data = SDL_LoadFile(path, &size);
    if (!data) {
        return false;
    }
    char section[128] = "";
    char *save = NULL;
    for (char *line = SDL_strtok_r(data, "\n", &save); line; line = SDL_strtok_r(NULL, "\n", &save)) {
        char *s = trim(line);
        if (*s == '\0' || *s == ';' || *s == '#') {
            continue;
        }
        if (*s == '[') {
            char *close = SDL_strchr(s, ']');
            if (close) {
                *close = '\0';
                SDL_strlcpy(section, trim(s + 1), sizeof(section));
            }
            continue;
        }
        char *eq = SDL_strchr(s, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        push(ini, section, trim(s), trim(eq + 1));
    }
    SDL_free(data);
    return true;
}

bool ini_save(const Ini *ini, const char *path)
{
    SDL_IOStream *io = SDL_IOFromFile(path, "w");
    if (!io) {
        return false;
    }
    const char *current = NULL;
    for (int i = 0; i < ini->count; i++) {
        const IniEntry *e = &ini->entries[i];
        if (!current || SDL_strcmp(current, e->section) != 0) {
            SDL_IOprintf(io, "%s[%s]\n", current ? "\n" : "", e->section);
            current = e->section;
        }
        SDL_IOprintf(io, "%s=%s\n", e->key, e->value);
    }
    return SDL_CloseIO(io);
}

void ini_free(Ini *ini)
{
    for (int i = 0; i < ini->count; i++) {
        SDL_free(ini->entries[i].section);
        SDL_free(ini->entries[i].key);
        SDL_free(ini->entries[i].value);
    }
    SDL_free(ini->entries);
    SDL_zerop(ini);
}

static IniEntry *find(const Ini *ini, const char *section, const char *key)
{
    for (int i = 0; i < ini->count; i++) {
        IniEntry *e = &ini->entries[i];
        if (SDL_strcmp(e->section, section) == 0 && SDL_strcmp(e->key, key) == 0) {
            return e;
        }
    }
    return NULL;
}

const char *ini_get(const Ini *ini, const char *section, const char *key, const char *fallback)
{
    IniEntry *e = find(ini, section, key);
    return e ? e->value : fallback;
}

int ini_get_int(const Ini *ini, const char *section, const char *key, int fallback)
{
    const char *v = ini_get(ini, section, key, NULL);
    return v ? SDL_atoi(v) : fallback;
}

void ini_set(Ini *ini, const char *section, const char *key, const char *value)
{
    IniEntry *e = find(ini, section, key);
    if (e) {
        SDL_free(e->value);
        e->value = SDL_strdup(value);
        return;
    }
    /* keep a section's keys together so ini_save writes one header per section */
    int insert = ini->count;
    for (int i = ini->count - 1; i >= 0; i--) {
        if (SDL_strcmp(ini->entries[i].section, section) == 0) {
            insert = i + 1;
            break;
        }
    }
    push(ini, section, key, value);
    IniEntry added = ini->entries[ini->count - 1];
    SDL_memmove(&ini->entries[insert + 1], &ini->entries[insert], sizeof(IniEntry) * (ini->count - 1 - insert));
    ini->entries[insert] = added;
}

void ini_each_section(const Ini *ini, void (*fn)(const char *section, void *user), void *user)
{
    for (int i = 0; i < ini->count; i++) {
        bool seen = false;
        for (int j = 0; j < i && !seen; j++) {
            seen = SDL_strcmp(ini->entries[j].section, ini->entries[i].section) == 0;
        }
        if (!seen) {
            fn(ini->entries[i].section, user);
        }
    }
}
