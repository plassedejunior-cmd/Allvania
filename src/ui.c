#include "ui.h"

#define FONT_CACHE_SIZE 48
#define CORNER_SEGMENTS 8

typedef struct CachedFont {
    FontFamily family;
    int pixels;
    bool bold;
    TTF_Font *font;
} CachedFont;

static CachedFont font_cache[FONT_CACHE_SIZE];
static int font_cache_count;

bool ui_init(Ui *ui, SDL_Renderer *renderer, const char *assets_dir)
{
    SDL_zerop(ui);
    ui->renderer = renderer;
    ui->scale = 1.0f;
    if (!TTF_Init()) {
        return false;
    }
    ui->text_engine = TTF_CreateRendererTextEngine(renderer);
    SDL_asprintf(&ui->font_paths[FONT_DISPLAY], "%sfonts/Cinzel.ttf", assets_dir);
    SDL_asprintf(&ui->font_paths[FONT_BODY], "%sfonts/Inter.ttf", assets_dir);
    return ui->text_engine != NULL;
}

void ui_shutdown(Ui *ui)
{
    for (int i = 0; i < font_cache_count; i++) {
        TTF_CloseFont(font_cache[i].font);
    }
    font_cache_count = 0;
    TTF_DestroyRendererTextEngine(ui->text_engine);
    for (int i = 0; i < FONT_FAMILY_COUNT; i++) {
        SDL_free(ui->font_paths[i]);
    }
    TTF_Quit();
}

static TTF_Font *get_font(Ui *ui, FontFamily family, float size, bool bold)
{
    int pixels = (int)SDL_roundf(size * ui->scale);
    for (int i = 0; i < font_cache_count; i++) {
        CachedFont *c = &font_cache[i];
        if (c->family == family && c->pixels == pixels && c->bold == bold) {
            return c->font;
        }
    }
    TTF_Font *font = TTF_OpenFont(ui->font_paths[family], (float)pixels);
    if (!font) {
        SDL_Log("font %s: %s", ui->font_paths[family], SDL_GetError());
        return NULL;
    }
    if (bold) {
        TTF_SetFontStyle(font, TTF_STYLE_BOLD);
    }
    if (font_cache_count == FONT_CACHE_SIZE) {
        /* sizes only churn when the window moves between displays; just start over */
        for (int i = 0; i < font_cache_count; i++) {
            TTF_CloseFont(font_cache[i].font);
        }
        font_cache_count = 0;
    }
    font_cache[font_cache_count++] = (CachedFont){ family, pixels, bold, font };
    return font;
}

static TTF_Text *make_text(Ui *ui, FontFamily family, float size, bool bold, const char *str, float wrap)
{
    TTF_Font *font = get_font(ui, family, size, bold);
    if (!font || !str || !*str) {
        return NULL;
    }
    TTF_Text *t = TTF_CreateText(ui->text_engine, font, str, 0);
    if (t && wrap > 0) {
        TTF_SetTextWrapWidth(t, (int)(wrap * ui->scale));
    }
    return t;
}

SDL_FPoint ui_text_size(Ui *ui, FontFamily family, float size, bool bold, const char *str, float wrap)
{
    TTF_Text *t = make_text(ui, family, size, bold, str, wrap);
    if (!t) {
        return (SDL_FPoint){ 0, 0 };
    }
    int w, h;
    TTF_GetTextSize(t, &w, &h);
    TTF_DestroyText(t);
    return (SDL_FPoint){ w / ui->scale, h / ui->scale };
}

SDL_FPoint ui_text(Ui *ui, FontFamily family, float size, bool bold, const char *str, float x, float y,
                   SDL_Color color, float wrap, TextAlign align)
{
    TTF_Text *t = make_text(ui, family, size, bold, str, wrap);
    if (!t) {
        return (SDL_FPoint){ 0, 0 };
    }
    int w, h;
    TTF_GetTextSize(t, &w, &h);
    float px = x * ui->scale;
    if (align == ALIGN_CENTER) {
        px -= w / 2.0f;
    } else if (align == ALIGN_RIGHT) {
        px -= (float)w;
    }
    TTF_SetTextColor(t, color.r, color.g, color.b, color.a);
    TTF_DrawRendererText(t, SDL_roundf(px), SDL_roundf(y * ui->scale));
    TTF_DestroyText(t);
    return (SDL_FPoint){ w / ui->scale, h / ui->scale };
}

static SDL_FRect to_pixels(Ui *ui, SDL_FRect r)
{
    return (SDL_FRect){ r.x * ui->scale, r.y * ui->scale, r.w * ui->scale, r.h * ui->scale };
}

void ui_fill(Ui *ui, SDL_FRect r, SDL_FColor color)
{
    SDL_FRect p = to_pixels(ui, r);
    SDL_SetRenderDrawColorFloat(ui->renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(ui->renderer, &p);
}

/* Builds a rounded-rect triangle fan in pixels. uv maps the rect onto [u0,v0]-[u1,v1] of a texture. */
static void rounded_geometry(Ui *ui, SDL_FRect r, float radius, SDL_Texture *tex, SDL_FRect uv, SDL_FColor top,
                             SDL_FColor bottom)
{
    SDL_FRect p = to_pixels(ui, r);
    float rad = SDL_min(radius * ui->scale, SDL_min(p.w, p.h) / 2.0f);
    const int n_outer = 4 * (CORNER_SEGMENTS + 1);
    SDL_Vertex v[1 + 4 * (CORNER_SEGMENTS + 1)];
    int idx[3 * 4 * (CORNER_SEGMENTS + 1)];

    const SDL_FPoint centers[4] = {
        { p.x + p.w - rad, p.y + rad },        /* top right */
        { p.x + p.w - rad, p.y + p.h - rad },  /* bottom right */
        { p.x + rad, p.y + p.h - rad },        /* bottom left */
        { p.x + rad, p.y + rad },              /* top left */
    };
    v[0].position = (SDL_FPoint){ p.x + p.w / 2, p.y + p.h / 2 };
    int k = 1;
    for (int c = 0; c < 4; c++) {
        float start = -SDL_PI_F / 2 + c * SDL_PI_F / 2;
        for (int s = 0; s <= CORNER_SEGMENTS; s++) {
            float a = start + (SDL_PI_F / 2) * s / CORNER_SEGMENTS;
            v[k++].position = (SDL_FPoint){ centers[c].x + SDL_cosf(a) * rad, centers[c].y + SDL_sinf(a) * rad };
        }
    }
    for (int i = 0; i <= n_outer; i++) {
        float f = (v[i].position.y - p.y) / p.h;
        v[i].color = (SDL_FColor){ top.r + (bottom.r - top.r) * f, top.g + (bottom.g - top.g) * f,
                                   top.b + (bottom.b - top.b) * f, top.a + (bottom.a - top.a) * f };
        v[i].tex_coord.x = uv.x + (v[i].position.x - p.x) / p.w * uv.w;
        v[i].tex_coord.y = uv.y + (v[i].position.y - p.y) / p.h * uv.h;
    }
    for (int i = 0; i < n_outer; i++) {
        idx[i * 3] = 0;
        idx[i * 3 + 1] = 1 + i;
        idx[i * 3 + 2] = 1 + (i + 1) % n_outer;
    }
    SDL_RenderGeometry(ui->renderer, tex, v, n_outer + 1, idx, n_outer * 3);
}

void ui_rounded_fill(Ui *ui, SDL_FRect r, float radius, SDL_FColor color)
{
    rounded_geometry(ui, r, radius, NULL, (SDL_FRect){ 0, 0, 1, 1 }, color, color);
}

void ui_rounded_vgradient(Ui *ui, SDL_FRect r, float radius, SDL_FColor top, SDL_FColor bottom)
{
    rounded_geometry(ui, r, radius, NULL, (SDL_FRect){ 0, 0, 1, 1 }, top, bottom);
}

void ui_rounded_outline(Ui *ui, SDL_FRect r, float radius, float width, SDL_FColor color)
{
    /* stroke as a ring of quads between an outer and an inner rounded path */
    SDL_FRect p = to_pixels(ui, r);
    float w = width * ui->scale;
    float rad = SDL_min(radius * ui->scale, SDL_min(p.w, p.h) / 2.0f);
    const int n = 4 * (CORNER_SEGMENTS + 1);
    SDL_Vertex v[2 * 4 * (CORNER_SEGMENTS + 1)];
    int idx[6 * 4 * (CORNER_SEGMENTS + 1)];
    const SDL_FPoint centers[4] = {
        { p.x + p.w - rad, p.y + rad },
        { p.x + p.w - rad, p.y + p.h - rad },
        { p.x + rad, p.y + p.h - rad },
        { p.x + rad, p.y + rad },
    };
    int k = 0;
    for (int c = 0; c < 4; c++) {
        float start = -SDL_PI_F / 2 + c * SDL_PI_F / 2;
        for (int s = 0; s <= CORNER_SEGMENTS; s++) {
            float a = start + (SDL_PI_F / 2) * s / CORNER_SEGMENTS;
            float ca = SDL_cosf(a), sa = SDL_sinf(a);
            v[k * 2].position = (SDL_FPoint){ centers[c].x + ca * rad, centers[c].y + sa * rad };
            /* callers keep radius >= width, so the inner path is the same arc shrunk by the stroke */
            v[k * 2 + 1].position = (SDL_FPoint){ centers[c].x + ca * SDL_max(rad - w, 0.0f),
                                                  centers[c].y + sa * SDL_max(rad - w, 0.0f) };
            v[k * 2].color = v[k * 2 + 1].color = color;
            v[k * 2].tex_coord = v[k * 2 + 1].tex_coord = (SDL_FPoint){ 0, 0 };
            k++;
        }
    }
    for (int i = 0; i < n; i++) {
        int o0 = i * 2, i0 = i * 2 + 1, o1 = ((i + 1) % n) * 2, i1 = ((i + 1) % n) * 2 + 1;
        int *q = &idx[i * 6];
        q[0] = o0;
        q[1] = o1;
        q[2] = i0;
        q[3] = i0;
        q[4] = o1;
        q[5] = i1;
    }
    SDL_RenderGeometry(ui->renderer, NULL, v, n * 2, idx, n * 6);
}

void ui_texture_cover(Ui *ui, SDL_Texture *tex, SDL_FRect r, float radius, float anchor_x, SDL_FColor tint)
{
    if (!tex) {
        return;
    }
    float tw, th;
    SDL_GetTextureSize(tex, &tw, &th);
    float scale = SDL_max(r.w / tw, r.h / th);
    float vis_w = r.w / scale / tw; /* visible fraction of the texture */
    float vis_h = r.h / scale / th;
    SDL_FRect uv = { (1 - vis_w) * anchor_x, (1 - vis_h) * 0.3f, vis_w, vis_h };
    rounded_geometry(ui, r, radius, tex, uv, tint, tint);
}

void ui_gradient(Ui *ui, SDL_FRect r, SDL_FColor top_left, SDL_FColor top_right, SDL_FColor bottom_left,
                 SDL_FColor bottom_right)
{
    SDL_FRect p = to_pixels(ui, r);
    SDL_Vertex v[4] = {
        { { p.x, p.y }, top_left, { 0, 0 } },
        { { p.x + p.w, p.y }, top_right, { 0, 0 } },
        { { p.x + p.w, p.y + p.h }, bottom_right, { 0, 0 } },
        { { p.x, p.y + p.h }, bottom_left, { 0, 0 } },
    };
    const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(ui->renderer, NULL, v, 4, idx, 6);
}

void ui_triangle(Ui *ui, SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_FColor color)
{
    float s = ui->scale;
    SDL_Vertex v[3] = {
        { { a.x * s, a.y * s }, color, { 0, 0 } },
        { { b.x * s, b.y * s }, color, { 0, 0 } },
        { { c.x * s, c.y * s }, color, { 0, 0 } },
    };
    SDL_RenderGeometry(ui->renderer, NULL, v, 3, NULL, 0);
}
