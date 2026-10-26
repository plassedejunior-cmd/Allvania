#ifndef LAUNCHER_SHA1_H
#define LAUNCHER_SHA1_H

#include <stdbool.h>

/* Hashes a whole file; writes 40 lowercase hex chars + NUL into out. */
bool sha1_file(const char *path, char out[41]);

#endif
