#ifndef LAUNCHER_UI_H
#define LAUNCHER_UI_H

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

typedef enum FontFamily {
    FONT_DISPLAY, /* Cinzel: titles and the wordmark */
    FONT_BODY,    /* Inter: everything else */
    FONT_FAMILY_COUNT,
} FontFamily;

typedef enum TextAlign {
    ALIGN_LEFT,
    ALIGN_CENTER,
    ALIGN_RIGHT,
} TextAlign;

/* All layout is written in logical points and multiplied by `scale` (pixels per point),
 * so the UI stays sharp on HiDPI displays. */
typedef struct Ui {
    SDL_Renderer *renderer;
    TTF_TextEngine *text_engine;
    float scale;
    char *font_paths[FONT_FAMILY_COUNT];
} Ui;

bool ui_init(Ui *ui, SDL_Renderer *renderer, const char *assets_dir);
void ui_shutdown(Ui *ui);

/* Draws text at (x, y) in points; returns its size in points. wrap = 0 disables wrapping. */
SDL_FPoint ui_text(Ui *ui, FontFamily family, float size, bool bold, const char *str, float x, float y,
                   SDL_Color color, float wrap, TextAlign align);
SDL_FPoint ui_text_size(Ui *ui, FontFamily family, float size, bool bold, const char *str, float wrap);

void ui_fill(Ui *ui, SDL_FRect r, SDL_FColor color);
void ui_rounded_fill(Ui *ui, SDL_FRect r, float radius, SDL_FColor color);
void ui_rounded_vgradient(Ui *ui, SDL_FRect r, float radius, SDL_FColor top, SDL_FColor bottom);
void ui_rounded_outline(Ui *ui, SDL_FRect r, float radius, float width, SDL_FColor color);
/* Draws tex cropped to cover r (keeping aspect), anchored horizontally at anchor_x in [0,1]. */
void ui_texture_cover(Ui *ui, SDL_Texture *tex, SDL_FRect r, float radius, float anchor_x, SDL_FColor tint);
void ui_gradient(Ui *ui, SDL_FRect r, SDL_FColor top_left, SDL_FColor top_right, SDL_FColor bottom_left,
                 SDL_FColor bottom_right);
void ui_triangle(Ui *ui, SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_FColor color);

static inline SDL_FColor ui_rgba(Uint8 r, Uint8 g, Uint8 b, float a)
{
    return (SDL_FColor){ r / 255.0f, g / 255.0f, b / 255.0f, a };
}

#endif
