#ifndef LAUNCHER_ART_H
#define LAUNCHER_ART_H

#include "games.h"
#include "ui.h"

/* Loads art/<id>.{png,jpg,jpeg,webp} from the user's art folder or next to the launcher;
 * falls back to a generated night-sky castle scene in the game's accent colours. */
SDL_Texture *art_load(Ui *ui, const Game *game);

/* Folder where the user can drop their own art (created if missing). Caller frees. */
char *art_user_dir(void);

#endif
