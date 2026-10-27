/* Castlevania Collection launcher: a Netflix-style shelf of the series' ports.
 * Each game is its own executable listed in games.ini; the launcher only finds the
 * user's ROM dump, picks the save folder (e.g. a Google Drive synced folder) and runs it. */

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <SDL3_image/SDL_image.h>

#include "art.h"
#include "games.h"
#include "sha1.h"
#include "ui.h"

#define BG_R 11
#define BG_G 11
#define BG_B 13

typedef enum DialogKind {
    DIALOG_NONE,
    DIALOG_ROM,
    DIALOG_SAVES,
} DialogKind;

typedef struct DialogResult {
    DialogKind kind;
    int game;
    char *path; /* NULL when cancelled */
} DialogResult;

typedef struct App {
    SDL_Window *window;
    SDL_Renderer *renderer;
    Ui ui;
    Library lib;
    SDL_Texture *art[MAX_GAMES];

    int selected;
    int hovered;
    float card_zoom[MAX_GAMES]; /* 0..1 hover/selection animation per card */
    float row_scroll, row_scroll_target;
    int hero_prev;
    float hero_fade; /* 0..1 crossfade from hero_prev to selected */

    /* hit areas from the last frame, in points */
    SDL_FRect card_rects[MAX_GAMES];
    SDL_FRect play_rect, rom_rect, saves_rect, layout_rect;
    SDL_FPoint mouse;

    char toast[256];
    Uint64 toast_until;

    SDL_Mutex *dialog_lock;
    DialogResult dialog;
    bool dialog_open;

    SDL_Process *process;
    int running_game;

    Uint64 last_ticks;

    /* --screenshot <file.png> [--select <n>]: render a few frames, save one, quit (for checking the UI headlessly) */
    const char *screenshot_path;
    int frames;
} App;

static void toast(App *app, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(app->toast, sizeof(app->toast), fmt, ap);
    va_end(ap);
    app->toast_until = SDL_GetTicks() + 4000;
}

static const char *basename_of(const char *path)
{
    const char *slash = SDL_strrchr(path, '/');
    const char *bslash = SDL_strrchr(path, '\\');
    const char *last = slash > bslash ? slash : bslash;
    return last ? last + 1 : path;
}

/* ---- dialogs (their callbacks may run on another thread) ---------------------------- */

static void SDLCALL dialog_done(void *userdata, const char *const *files, int filter)
{
    (void)filter;
    App *app = userdata;
    SDL_LockMutex(app->dialog_lock);
    SDL_free(app->dialog.path);
    app->dialog.path = files && files[0] ? SDL_strdup(files[0]) : NULL;
    app->dialog_open = false;
    SDL_UnlockMutex(app->dialog_lock);
}

static void open_rom_dialog(App *app, int game)
{
    static const SDL_DialogFileFilter filters[] = { { "Nintendo DS ROM", "nds" } };
    if (app->dialog_open) {
        return;
    }
    app->dialog_open = true;
    app->dialog.kind = DIALOG_ROM;
    app->dialog.game = game;
    SDL_ShowOpenFileDialog(dialog_done, app, app->window, filters, 1, library_rom_path(&app->lib, &app->lib.games[game]),
                           false);
}

static void open_saves_dialog(App *app)
{
    if (app->dialog_open) {
        return;
    }
    char *drive = library_detect_drive_folder();
    app->dialog_open = true;
    app->dialog.kind = DIALOG_SAVES;
    SDL_ShowOpenFolderDialog(dialog_done, app, app->window,
                             library_save_root_is_custom(&app->lib) ? library_save_root(&app->lib) : drive, false);
    SDL_free(drive);
}

static void handle_dialog_result(App *app)
{
    SDL_LockMutex(app->dialog_lock);
    if (app->dialog_open || app->dialog.kind == DIALOG_NONE) {
        SDL_UnlockMutex(app->dialog_lock);
        return;
    }
    DialogResult res = app->dialog;
    app->dialog.kind = DIALOG_NONE;
    app->dialog.path = NULL;
    SDL_UnlockMutex(app->dialog_lock);

    if (res.path && res.kind == DIALOG_ROM) {
        const Game *g = &app->lib.games[res.game];
        char sha1[41];
        if (!sha1_file(res.path, sha1)) {
            toast(app, "Não consegui ler %s", basename_of(res.path));
        } else {
            bool ok = !g->rom_sha1 || SDL_strcasecmp(sha1, g->rom_sha1) == 0;
            library_set_rom(&app->lib, g, res.path, ok);
            if (ok) {
                toast(app, "ROM verificada: %s", basename_of(res.path));
            } else {
                toast(app, "Este dump não é o esperado (SHA1 %.8s…). O port precisa da versão USA.", sha1);
            }
        }
    } else if (res.path && res.kind == DIALOG_SAVES) {
        library_set_save_root(&app->lib, res.path);
        toast(app, "Saves agora em %s", res.path);
    }
    SDL_free(res.path);
}

/* ---- launching --------------------------------------------------------------------- */

static void launch(App *app, int index)
{
    const Game *g = &app->lib.games[index];
    if (g->status != GAME_AVAILABLE) {
        toast(app, "%s ainda não tem port. Em breve!", g->title);
        return;
    }
    if (!library_rom_verified(&app->lib, g)) {
        open_rom_dialog(app, index);
        return;
    }
    if (!g->exe) {
        toast(app, "O port de %s ainda não foi compilado.", g->title);
        return;
    }
    const char *root = library_save_root(&app->lib);
    size_t root_len = SDL_strlen(root);
    while (root_len > 1 && (root[root_len - 1] == '/' || root[root_len - 1] == '\\')) {
        root_len--;
    }
    char *save_dir;
    SDL_asprintf(&save_dir, "%.*s/%s", (int)root_len, root, g->id);
    SDL_CreateDirectory(save_dir);
    const char *layout = library_split_screens(&app->lib, g) ? "split" : "original";
    const char *args[] = { g->exe, "--rom", library_rom_path(&app->lib, g), "--save-dir", save_dir, "--layout", layout,
                           g->text ? "--text" : NULL, g->text, NULL };
    app->process = SDL_CreateProcess(args, false);
    SDL_free(save_dir);
    if (!app->process) {
        toast(app, "Falha ao abrir o jogo: %s", SDL_GetError());
        return;
    }
    app->running_game = index;
    SDL_HideWindow(app->window);
}

static void poll_process(App *app)
{
    int code = 0;
    if (!app->process || !SDL_WaitProcess(app->process, false, &code)) {
        return;
    }
    SDL_DestroyProcess(app->process);
    app->process = NULL;
    SDL_ShowWindow(app->window);
    SDL_RaiseWindow(app->window);
    if (code != 0) {
        toast(app, "%s fechou com erro (código %d)", app->lib.games[app->running_game].title, code);
    }
}

/* ---- input ------------------------------------------------------------------------- */

static void toggle_layout(App *app, int index)
{
    const Game *g = &app->lib.games[index];
    if (!g->dual_screen) {
        return;
    }
    bool split = !library_split_screens(&app->lib, g);
    library_set_split_screens(&app->lib, g, split);
    toast(app, split ? "Telas separadas: cada tela abre na sua janela (Tab mostra ou esconde a de baixo)"
                     : "Telas originais: as duas juntas na mesma janela");
}

static void select_game(App *app, int index)
{
    if (index < 0 || index >= app->lib.count || index == app->selected) {
        return;
    }
    app->hero_prev = app->selected;
    app->hero_fade = 0;
    app->selected = index;
}

static bool hit(SDL_FRect r, SDL_FPoint p)
{
    return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
}

static void click(App *app, SDL_FPoint p)
{
    if (hit(app->play_rect, p)) {
        launch(app, app->selected);
        return;
    }
    if (hit(app->rom_rect, p)) {
        open_rom_dialog(app, app->selected);
        return;
    }
    if (hit(app->saves_rect, p)) {
        open_saves_dialog(app);
        return;
    }
    if (hit(app->layout_rect, p)) {
        toggle_layout(app, app->selected);
        return;
    }
    for (int i = 0; i < app->lib.count; i++) {
        if (hit(app->card_rects[i], p)) {
            if (i == app->selected) {
                launch(app, i);
            } else {
                select_game(app, i);
            }
            return;
        }
    }
}

/* ---- drawing ----------------------------------------------------------------------- */

static const SDL_Color WHITE = { 255, 255, 255, 255 };
static const SDL_Color MUTED = { 170, 170, 176, 255 };
static const SDL_Color CRIMSON = { 196, 22, 28, 255 };

static float ease(float t)
{
    return t * t * (3 - 2 * t);
}

static void draw_button(App *app, SDL_FRect r, bool primary, bool enabled, bool play_icon, const char *label)
{
    Ui *ui = &app->ui;
    bool hover = enabled && hit(r, app->mouse);
    SDL_FColor bg = primary ? ui_rgba(255, 255, 255, enabled ? (hover ? 0.8f : 1) : 0.35f)
                            : ui_rgba(109, 109, 110, hover ? 0.55f : 0.7f);
    ui_rounded_fill(ui, r, 6, bg);
    SDL_Color fg = primary ? (SDL_Color){ 0, 0, 0, 255 } : WHITE;
    SDL_FPoint size = ui_text_size(ui, FONT_BODY, 17, true, label, 0);
    float content = size.x + (play_icon ? 26 : 0);
    float x = r.x + (r.w - content) / 2;
    if (play_icon) {
        float cy = r.y + r.h / 2;
        ui_triangle(ui, (SDL_FPoint){ x, cy - 10 }, (SDL_FPoint){ x + 17, cy }, (SDL_FPoint){ x, cy + 10 },
                    ui_rgba(fg.r, fg.g, fg.b, 1));
        x += 26;
    }
    ui_text(ui, FONT_BODY, 17, true, label, x, r.y + (r.h - size.y) / 2, fg, 0, ALIGN_LEFT);
}

static void draw_hero(App *app, float W, float hero_h, float text_bottom)
{
    Ui *ui = &app->ui;
    SDL_FRect hero = { 0, 0, W, hero_h };
    float f = ease(app->hero_fade);
    if (f < 1 && app->hero_prev >= 0) {
        ui_texture_cover(ui, app->art[app->hero_prev], hero, 0, 0.7f, (SDL_FColor){ 1, 1, 1, 1 });
    }
    ui_texture_cover(ui, app->art[app->selected], hero, 0, 0.7f, (SDL_FColor){ 1, 1, 1, f });

    SDL_FColor bg = ui_rgba(BG_R, BG_G, BG_B, 1), clear = ui_rgba(BG_R, BG_G, BG_B, 0);
    SDL_FColor bg_soft = ui_rgba(BG_R, BG_G, BG_B, 0.92f);
    ui_gradient(ui, (SDL_FRect){ 0, 0, W * 0.62f, hero_h }, bg_soft, clear, bg_soft, clear);
    ui_gradient(ui, (SDL_FRect){ 0, hero_h * 0.55f, W, hero_h * 0.45f + 1 }, clear, clear, bg, bg);
    ui_gradient(ui, (SDL_FRect){ 0, 0, W, 110 }, ui_rgba(0, 0, 0, 0.7f), ui_rgba(0, 0, 0, 0.7f), clear, clear);

    const Game *g = &app->lib.games[app->selected];
    float x = 56, wrap = SDL_min(W * 0.46f, 600.0f);
    float title_size = W < 1200 ? 46.0f : 54.0f;
    bool available = g->status == GAME_AVAILABLE;
    float block = ui_text_size(ui, FONT_DISPLAY, 18, false, g->series, 0).y +
                  ui_text_size(ui, FONT_DISPLAY, title_size, true, g->title, wrap).y + 8 +
                  ui_text_size(ui, FONT_BODY, 15, false, "2008", 0).y + 16 +
                  ui_text_size(ui, FONT_BODY, 16, false, g->description, wrap).y + 24 + 48 +
                  (available ? 14 + ui_text_size(ui, FONT_BODY, 13, false, "Ag", 0).y : 0);
    float y = SDL_max(text_bottom - block, 92.0f);
    y += ui_text(ui, FONT_DISPLAY, 18, false, g->series, x, y, (SDL_Color){ 220, 210, 200, 255 }, 0, ALIGN_LEFT).y;
    y += ui_text(ui, FONT_DISPLAY, title_size, true, g->title, x - 2, y, WHITE, wrap, ALIGN_LEFT).y + 8;

    char meta[128];
    SDL_snprintf(meta, sizeof(meta), "%s   •   %s", g->year, g->platform);
    SDL_FPoint m = ui_text(ui, FONT_BODY, 15, false, meta, x, y, MUTED, 0, ALIGN_LEFT);
    const char *badge = g->status == GAME_AVAILABLE ? "JOGÁVEL" : "EM BREVE";
    SDL_FPoint bs = ui_text_size(ui, FONT_BODY, 11, true, badge, 0);
    SDL_FRect pill = { x + m.x + 14, y + (m.y - bs.y - 6) / 2, bs.x + 14, bs.y + 6 };
    ui_rounded_fill(ui, pill, 4, g->status == GAME_AVAILABLE ? ui_rgba(196, 22, 28, 0.9f) : ui_rgba(90, 90, 96, 0.9f));
    ui_text(ui, FONT_BODY, 11, true, badge, pill.x + 7, pill.y + 3, WHITE, 0, ALIGN_LEFT);
    y += m.y + 16;

    y += ui_text(ui, FONT_BODY, 16, false, g->description, x, y, (SDL_Color){ 229, 229, 229, 255 }, wrap, ALIGN_LEFT).y + 24;

    bool verified = library_rom_verified(&app->lib, g);
    app->play_rect = (SDL_FRect){ x, y, verified ? 150.0f : 200.0f, 48 };
    app->rom_rect = (SDL_FRect){ 0, 0, 0, 0 };
    app->layout_rect = (SDL_FRect){ 0, 0, 0, 0 };
    if (!available) {
        draw_button(app, app->play_rect, true, false, false, "Em breve");
    } else {
        draw_button(app, app->play_rect, true, true, verified, verified ? "Jogar" : "Localizar ROM");
        float next_x = app->play_rect.x + app->play_rect.w + 12;
        if (verified) {
            app->rom_rect = (SDL_FRect){ next_x, y, 170, 48 };
            draw_button(app, app->rom_rect, false, true, false, "Trocar ROM");
            next_x += app->rom_rect.w + 12;
        }
        if (g->dual_screen) {
            app->layout_rect = (SDL_FRect){ next_x, y, 220, 48 };
            draw_button(app, app->layout_rect, false, true, false,
                        library_split_screens(&app->lib, g) ? "Telas: separadas" : "Telas: original");
        }
    }
    y += 48 + 14;

    if (available) {
        const char *rom = library_rom_path(&app->lib, g);
        char status[512];
        SDL_Color c = MUTED;
        if (verified && !g->exe) {
            SDL_snprintf(status, sizeof(status), "ROM verificada (%s). O executável do port ainda não foi compilado.",
                         basename_of(rom));
            c = (SDL_Color){ 230, 180, 90, 255 };
        } else if (verified) {
            SDL_snprintf(status, sizeof(status), "ROM verificada: %s", basename_of(rom));
            c = (SDL_Color){ 120, 200, 140, 255 };
        } else if (rom) {
            SDL_snprintf(status, sizeof(status), "%s não bate com o dump esperado (USA, SHA1 %.8s…)", basename_of(rom),
                         g->rom_sha1 ? g->rom_sha1 : "?");
            c = (SDL_Color){ 230, 120, 100, 255 };
        } else {
            SDL_snprintf(status, sizeof(status), "Aponte para o .nds que você extraiu do seu cartucho.");
        }
        ui_text(ui, FONT_BODY, 13, false, status, x, y, c, wrap, ALIGN_LEFT);
    }
}

static void draw_top_bar(App *app, float W)
{
    Ui *ui = &app->ui;
    SDL_FPoint w = ui_text(ui, FONT_DISPLAY, 26, true, "CASTLEVANIA", 48, 24, CRIMSON, 0, ALIGN_LEFT);
    ui_text(ui, FONT_BODY, 11, true, "C O L L E C T I O N", 50 + w.x + 12, 24 + w.y - 20, MUTED, 0, ALIGN_LEFT);

    bool custom = library_save_root_is_custom(&app->lib);
    const char *root = library_save_root(&app->lib);
    char label[256];
    bool drive = custom && (SDL_strstr(root, "GoogleDrive") || SDL_strstr(root, "My Drive") || SDL_strstr(root, "Meu Drive"));
    SDL_snprintf(label, sizeof(label), "Saves: %s", !custom ? "neste computador" : drive ? "Google Drive" : basename_of(root));
    SDL_FPoint s = ui_text_size(ui, FONT_BODY, 13, true, label, 0);
    app->saves_rect = (SDL_FRect){ W - 48 - s.x - 28, 26, s.x + 28, 32 };
    bool hover = hit(app->saves_rect, app->mouse);
    ui_rounded_fill(ui, app->saves_rect, 16, ui_rgba(255, 255, 255, hover ? 0.22f : 0.12f));
    if (drive) {
        ui_rounded_fill(ui, (SDL_FRect){ app->saves_rect.x + 10, app->saves_rect.y + 12, 8, 8 }, 4, ui_rgba(120, 200, 140, 1));
    }
    ui_text(ui, FONT_BODY, 13, true, label, app->saves_rect.x + (drive ? 24 : 14), app->saves_rect.y + (32 - s.y) / 2, WHITE,
            0, ALIGN_LEFT);
}

static void draw_row(App *app, float W, float row_y, float card_w, float dt)
{
    Ui *ui = &app->ui;
    float card_h = card_w * 9 / 16, gap = 18;
    ui_text(ui, FONT_BODY, 20, true, "Coleção", 56, row_y - 34, WHITE, 0, ALIGN_LEFT);

    /* keep the selected card on screen */
    float left = 56 + app->selected * (card_w + gap);
    if (left - app->row_scroll_target > W - card_w - 56) {
        app->row_scroll_target = left - (W - card_w - 56);
    }
    if (left - app->row_scroll_target < 56) {
        app->row_scroll_target = left - 56;
    }
    app->row_scroll += (app->row_scroll_target - app->row_scroll) * SDL_min(1.0f, dt * 12);

    /* draw the zoomed card last so it overlaps its neighbours */
    int order[MAX_GAMES], n = 0, top = -1;
    for (int i = 0; i < app->lib.count; i++) {
        float target = (i == app->selected || i == app->hovered) ? 1.0f : 0.0f;
        app->card_zoom[i] += (target - app->card_zoom[i]) * SDL_min(1.0f, dt * 10);
        if (i == app->selected) {
            top = i;
        } else {
            order[n++] = i;
        }
    }
    if (top >= 0) {
        order[n++] = top;
    }

    for (int k = 0; k < n; k++) {
        int i = order[k];
        const Game *g = &app->lib.games[i];
        float z = ease(app->card_zoom[i]);
        float s = 1 + 0.06f * z;
        SDL_FRect base = { 56 + i * (card_w + gap) - app->row_scroll, row_y, card_w, card_h };
        SDL_FRect r = { base.x - base.w * (s - 1) / 2, base.y - base.h * (s - 1) / 2, base.w * s, base.h * s };
        app->card_rects[i] = base;

        ui_rounded_fill(ui, (SDL_FRect){ r.x + 2, r.y + 8, r.w, r.h }, 8, ui_rgba(0, 0, 0, 0.35f + 0.25f * z));
        float dim = g->status == GAME_AVAILABLE ? 1.0f : 0.55f;
        ui_texture_cover(ui, app->art[i], r, 8, 0.65f, (SDL_FColor){ dim, dim, dim, 1 });
        ui_rounded_vgradient(ui, r, 8, ui_rgba(0, 0, 0, 0), ui_rgba(0, 0, 0, 0.85f));

        float tx = r.x + 16, ty = r.y + r.h - 16;
        SDL_FPoint ts = ui_text_size(ui, FONT_DISPLAY, 22 * s, true, g->title, r.w - 32);
        ui_text(ui, FONT_DISPLAY, 22 * s, true, g->title, tx, ty - ts.y, WHITE, r.w - 32, ALIGN_LEFT);
        SDL_FPoint ss = ui_text_size(ui, FONT_DISPLAY, 12 * s, false, g->series, 0);
        ui_text(ui, FONT_DISPLAY, 12 * s, false, g->series, tx, ty - ts.y - ss.y, (SDL_Color){ 210, 200, 190, 255 }, 0,
                ALIGN_LEFT);

        if (g->status != GAME_AVAILABLE) {
            SDL_FPoint bs = ui_text_size(ui, FONT_BODY, 11, true, "EM BREVE", 0);
            SDL_FRect pill = { r.x + 12, r.y + 12, bs.x + 14, bs.y + 6 };
            ui_rounded_fill(ui, pill, 4, ui_rgba(196, 22, 28, 0.95f));
            ui_text(ui, FONT_BODY, 11, true, "EM BREVE", pill.x + 7, pill.y + 3, WHITE, 0, ALIGN_LEFT);
        }
        if (i == app->selected) {
            ui_rounded_outline(ui, r, 8, 3, ui_rgba(255, 255, 255, 0.35f + 0.65f * z));
        }
    }
}

static void draw(App *app, float dt)
{
    int pw, ph, ww, wh;
    SDL_GetCurrentRenderOutputSize(app->renderer, &pw, &ph);
    SDL_GetWindowSize(app->window, &ww, &wh);
    app->ui.scale = ww > 0 ? (float)pw / ww : 1.0f;
    float W = (float)ww, H = (float)wh;

    SDL_SetRenderDrawColor(app->renderer, BG_R, BG_G, BG_B, 255);
    SDL_RenderClear(app->renderer);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);

    float card_w = SDL_clamp(W * 0.25f, 260.0f, 440.0f);
    float row_y = H - card_w * 9 / 16 - 72;
    float hero_h = row_y + 40;
    draw_hero(app, W, hero_h, row_y - 64);
    draw_top_bar(app, W);
    draw_row(app, W, row_y, card_w, dt);

    ui_text(&app->ui, FONT_BODY, 12, false, "←/→ escolher    Enter jogar    R trocar ROM    T telas    S pasta de saves", 56, H - 30,
            (SDL_Color){ 110, 110, 116, 255 }, 0, ALIGN_LEFT);

    if (SDL_GetTicks() < app->toast_until) {
        SDL_FPoint s = ui_text_size(&app->ui, FONT_BODY, 14, false, app->toast, W - 160);
        SDL_FRect box = { (W - s.x) / 2 - 18, H - 92 - s.y, s.x + 36, s.y + 24 };
        ui_rounded_fill(&app->ui, box, 8, ui_rgba(32, 32, 36, 0.96f));
        ui_text(&app->ui, FONT_BODY, 14, false, app->toast, box.x + 18, box.y + 12, WHITE, W - 160, ALIGN_LEFT);
    }
}

/* ---- SDL app callbacks ------------------------------------------------------------- */

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    SDL_SetAppMetadata("Castlevania Collection", "0.1", "com.castlevania.collection");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    App *app = SDL_calloc(1, sizeof(App));
    *appstate = app;
    app->hero_prev = -1;
    app->hovered = -1;
    app->running_game = -1;
    app->dialog_lock = SDL_CreateMutex();

    if (!SDL_CreateWindowAndRenderer("Castlevania Collection", 1280, 800,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &app->window, &app->renderer)) {
        SDL_Log("window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetWindowMinimumSize(app->window, 960, 600);
    SDL_SetRenderVSync(app->renderer, 1);

    char *assets;
    SDL_asprintf(&assets, "%sassets/", SDL_GetBasePath());
    bool ui_ok = ui_init(&app->ui, app->renderer, assets);
    SDL_free(assets);
    if (!ui_ok || !library_load(&app->lib) || app->lib.count == 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Castlevania Collection",
                                 "Não encontrei assets/ ou games.ini ao lado do launcher.", app->window);
        return SDL_APP_FAILURE;
    }
    for (int i = 0; i < app->lib.count; i++) {
        app->art[i] = art_load(&app->ui, &app->lib.games[i]);
    }
    app->hero_fade = 1;
    app->last_ticks = SDL_GetTicks();
    for (int i = 1; i + 1 < argc; i++) {
        if (SDL_strcmp(argv[i], "--screenshot") == 0) {
            app->screenshot_path = argv[i + 1];
        } else if (SDL_strcmp(argv[i], "--select") == 0) {
            app->selected = SDL_clamp(SDL_atoi(argv[i + 1]), 0, app->lib.count - 1);
        } else if (SDL_strcmp(argv[i], "--launch") == 0) {
            /* --launch <id>: start a game straight away (desktop shortcuts) */
            for (int g = 0; g < app->lib.count; g++) {
                if (SDL_strcmp(app->lib.games[g].id, argv[i + 1]) == 0) {
                    app->selected = g;
                    launch(app, g);
                }
            }
        }
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    App *app = appstate;
    switch (event->type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;
    case SDL_EVENT_MOUSE_MOTION:
        app->mouse = (SDL_FPoint){ event->motion.x, event->motion.y };
        app->hovered = -1;
        for (int i = 0; i < app->lib.count; i++) {
            if (hit(app->card_rects[i], app->mouse)) {
                app->hovered = i;
            }
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event->button.button == SDL_BUTTON_LEFT) {
            click(app, (SDL_FPoint){ event->button.x, event->button.y });
        }
        break;
    case SDL_EVENT_KEY_DOWN:
        switch (event->key.key) {
        case SDLK_LEFT:
            select_game(app, app->selected - 1);
            break;
        case SDLK_RIGHT:
            select_game(app, app->selected + 1);
            break;
        case SDLK_RETURN:
        case SDLK_SPACE:
            launch(app, app->selected);
            break;
        case SDLK_R:
            if (app->lib.games[app->selected].status == GAME_AVAILABLE) {
                open_rom_dialog(app, app->selected);
            }
            break;
        case SDLK_S:
            open_saves_dialog(app);
            break;
        case SDLK_T:
            if (app->lib.games[app->selected].status == GAME_AVAILABLE) {
                toggle_layout(app, app->selected);
            }
            break;
        default:
            break;
        }
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        SDL_OpenGamepad(event->gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        switch (event->gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            select_game(app, app->selected - 1);
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            select_game(app, app->selected + 1);
            break;
        case SDL_GAMEPAD_BUTTON_SOUTH:
        case SDL_GAMEPAD_BUTTON_START:
            launch(app, app->selected);
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    App *app = appstate;
    Uint64 now = SDL_GetTicks();
    float dt = (now - app->last_ticks) / 1000.0f;
    app->last_ticks = now;

    handle_dialog_result(app);
    poll_process(app);
    if (app->process) {
        SDL_Delay(100); /* hidden while the game runs */
        return SDL_APP_CONTINUE;
    }
    app->hero_fade = SDL_min(1.0f, app->hero_fade + dt * 3.5f);
    draw(app, dt);
    if (app->screenshot_path && ++app->frames == 60) {
        SDL_Surface *shot = SDL_RenderReadPixels(app->renderer, NULL);
        bool ok = shot && IMG_SavePNG(shot, app->screenshot_path);
        SDL_DestroySurface(shot);
        return ok ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }
    SDL_RenderPresent(app->renderer);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    (void)result;
    App *app = appstate;
    if (!app) {
        return;
    }
    for (int i = 0; i < app->lib.count; i++) {
        SDL_DestroyTexture(app->art[i]);
    }
    library_free(&app->lib);
    ui_shutdown(&app->ui);
    SDL_DestroyMutex(app->dialog_lock);
    SDL_free(app);
}
