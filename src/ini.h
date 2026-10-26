#ifndef LAUNCHER_INI_H
#define LAUNCHER_INI_H

#include <stdbool.h>

/* Minimal INI store: [section] headers, key=value lines, ';' or '#' comments. */

typedef struct IniEntry {
    char *section;
    char *key;
    char *value;
} IniEntry;

typedef struct Ini {
    IniEntry *entries;
    int count;
    int capacity;
} Ini;

bool ini_load(Ini *ini, const char *path);
bool ini_save(const Ini *ini, const char *path);
void ini_free(Ini *ini);

const char *ini_get(const Ini *ini, const char *section, const char *key, const char *fallback);
int ini_get_int(const Ini *ini, const char *section, const char *key, int fallback);
void ini_set(Ini *ini, const char *section, const char *key, const char *value);

/* Calls fn once per distinct section name, in file order. */
void ini_each_section(const Ini *ini, void (*fn)(const char *section, void *user), void *user);

#endif
