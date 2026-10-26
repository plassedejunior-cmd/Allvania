#include "art.h"

#include <SDL3_image/SDL_image.h>

#define ART_W 1600
#define ART_H 900

char *art_user_dir(void)
{
    char *pref = SDL_GetPrefPath("CastlevaniaCollection", "launcher");
    if (!pref) {
        return NULL;
    }
    char *dir;
    SDL_asprintf(&dir, "%sart", pref);
    SDL_free(pref);
    SDL_CreateDirectory(dir);
    return dir;
}

static SDL_Texture *try_load(SDL_Renderer *r, const char *dir, const char *id)
{
    static const char *exts[] = { "png", "jpg", "jpeg", "webp" };
    for (size_t i = 0; i < SDL_arraysize(exts); i++) {
        char *path;
        SDL_asprintf(&path, "%s/%s.%s", dir, id, exts[i]);
        SDL_Texture *tex = IMG_LoadTexture(r, path);
        SDL_free(path);
        if (tex) {
            return tex;
        }
    }
    return NULL;
}

static float frand(Uint32 *seed)
{
    *seed = *seed * 1664525u + 1013904223u;
    return (*seed >> 8) / 16777216.0f;
}

static void disc(Ui *ui, float cx, float cy, float radius, SDL_FColor inner, SDL_FColor outer)
{
    enum { SEGMENTS = 64 };
    SDL_Vertex v[SEGMENTS + 1];
    int idx[SEGMENTS * 3];
    v[0] = (SDL_Vertex){ { cx, cy }, inner, { 0, 0 } };
    for (int i = 0; i < SEGMENTS; i++) {
        float a = 2 * SDL_PI_F * i / SEGMENTS;
        v[i + 1] = (SDL_Vertex){ { cx + SDL_cosf(a) * radius, cy + SDL_sinf(a) * radius }, outer, { 0, 0 } };
        idx[i * 3] = 0;
        idx[i * 3 + 1] = 1 + i;
        idx[i * 3 + 2] = 1 + (i + 1) % SEGMENTS;
    }
    SDL_RenderGeometry(ui->renderer, NULL, v, SEGMENTS + 1, idx, SEGMENTS * 3);
}

/* Castle silhouette: towers with pointed roofs and lit windows along the bottom edge. */
static void castle(Ui *ui, Uint32 seed, SDL_FColor body)
{
    SDL_Renderer *r = ui->renderer;
    float x = ART_W * 0.38f;
    while (x < ART_W) {
        float w = 40 + frand(&seed) * 90;
        float h = 120 + frand(&seed) * 360;
        float top = ART_H - h;
        ui_fill(ui, (SDL_FRect){ x, top, w, h }, body);
        float roof = 40 + frand(&seed) * 120;
        ui_triangle(ui, (SDL_FPoint){ x - 8, top }, (SDL_FPoint){ x + w / 2, top - roof }, (SDL_FPoint){ x + w + 8, top },
                    body);
        for (int i = 0; i < 3; i++) {
            if (frand(&seed) < 0.45f) {
                SDL_FRect win = { x + w * (0.3f + 0.2f * i), top + 30 + frand(&seed) * (h * 0.5f), 6, 14 };
                SDL_SetRenderDrawColorFloat(r, 1.0f, 0.72f, 0.35f, 0.75f);
                SDL_RenderFillRect(r, &win);
            }
        }
        x += w + frand(&seed) * 50;
    }
    /* curtain wall tying the towers together */
    ui_fill(ui, (SDL_FRect){ ART_W * 0.35f, ART_H - 90, ART_W * 0.65f, 90 }, body);
}

static SDL_Texture *generate(Ui *ui, const Game *game)
{
    SDL_Renderer *r = ui->renderer;
    SDL_Texture *tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, ART_W, ART_H);
    if (!tex) {
        return NULL;
    }
    SDL_Texture *prev = SDL_GetRenderTarget(r);
    float prev_scale = ui->scale;
    ui->scale = 1.0f;
    SDL_SetRenderTarget(r, tex);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    SDL_Color a = game->accent[0], b = game->accent[1];
    SDL_FColor ca = ui_rgba(a.r, a.g, a.b, 1), cb = ui_rgba(b.r, b.g, b.b, 1);
    SDL_FColor dark = ui_rgba(a.r / 4, a.g / 4, a.b / 4, 1);
    ui_gradient(ui, (SDL_FRect){ 0, 0, ART_W, ART_H }, dark, ca, cb, dark);

    Uint32 seed = 2166136261u;
    for (const char *p = game->id; *p; p++) {
        seed = (seed ^ (Uint8)*p) * 16777619u;
    }
    /* stars */
    for (int i = 0; i < 140; i++) {
        float sx = frand(&seed) * ART_W, sy = frand(&seed) * ART_H * 0.6f, s = 1 + frand(&seed) * 2;
        SDL_SetRenderDrawColorFloat(r, 1, 1, 1, 0.25f + frand(&seed) * 0.5f);
        SDL_RenderFillRect(r, &(SDL_FRect){ sx, sy, s, s });
    }
    /* moon with a soft halo */
    float mx = ART_W * (0.62f + frand(&seed) * 0.2f), my = ART_H * (0.22f + frand(&seed) * 0.1f);
    disc(ui, mx, my, 260, ui_rgba(255, 240, 220, 0.18f), ui_rgba(255, 240, 220, 0));
    disc(ui, mx, my, 92, ui_rgba(255, 246, 232, 1), ui_rgba(238, 222, 200, 1));
    castle(ui, seed, ui_rgba(8, 6, 10, 1));
    /* haze over the castle base */
    ui_gradient(ui, (SDL_FRect){ 0, ART_H * 0.62f, ART_W, ART_H * 0.38f }, ui_rgba(0, 0, 0, 0), ui_rgba(0, 0, 0, 0),
                ui_rgba(b.r / 3, b.g / 3, b.b / 3, 0.85f), ui_rgba(b.r / 3, b.g / 3, b.b / 3, 0.85f));

    SDL_SetRenderTarget(r, prev);
    ui->scale = prev_scale;
    return tex;
}

SDL_Texture *art_load(Ui *ui, const Game *game)
{
    char *user_dir = art_user_dir();
    SDL_Texture *tex = user_dir ? try_load(ui->renderer, user_dir, game->id) : NULL;
    SDL_free(user_dir);
    if (!tex) {
        char *bundled;
        SDL_asprintf(&bundled, "%sart", SDL_GetBasePath());
        tex = try_load(ui->renderer, bundled, game->id);
        SDL_free(bundled);
    }
    if (!tex) {
        tex = generate(ui, game);
    }
    if (tex) {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    }
    return tex;
}
