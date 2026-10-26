/* Order of Ecclesia PC port: entry point.
 *
 * Launched by the Castlevania Collection launcher as:
 *   ooe --rom <path to the user's .nds dump> --save-dir <folder for saves>
 *
 * For now this only proves the plumbing: it reads the dump, shows the DS's two screens
 * and the banner icon taken from the ROM. The game core (original ARM code running on an
 * embedded interpreter, replaced function by function with decompiled C) comes next. */

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "rom.h"

#define SCREEN_W 256
#define SCREEN_H 192
#define SCREEN_GAP 8 /* points between the two screens, like the hinge */

typedef struct Port {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *screens[2]; /* 0 = top, 1 = bottom; RGBA8888 framebuffers */
    Uint32 framebuffer[2][SCREEN_W * SCREEN_H];
    NdsRom rom;
    char *save_dir;
    const char *screenshot_path; /* --screenshot <file.bmp>: save the first frames and quit (for automated checks) */
    int frames;
} Port;

static bool parse_args(int argc, char **argv, const char **rom, const char **save_dir, const char **screenshot)
{
    for (int i = 1; i + 1 < argc; i++) {
        if (SDL_strcmp(argv[i], "--rom") == 0) {
            *rom = argv[++i];
        } else if (SDL_strcmp(argv[i], "--save-dir") == 0) {
            *save_dir = argv[++i];
        } else if (SDL_strcmp(argv[i], "--screenshot") == 0) {
            *screenshot = argv[++i];
        }
    }
    return *rom != NULL;
}

static void fail(const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    SDL_Log("%s", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Order of Ecclesia", msg, NULL);
}

/* Placeholder frame until the game core draws: night-blue top screen with the ROM's banner icon. */
static void draw_placeholder(Port *port)
{
    Uint32 icon[32 * 32];
    bool have_icon = rom_banner_icon(&port->rom, icon);
    for (int s = 0; s < 2; s++) {
        for (int y = 0; y < SCREEN_H; y++) {
            Uint8 v = (Uint8)(14 + y * 30 / SCREEN_H);
            Uint32 c = (Uint32)(v / 2) << 24 | (Uint32)(v / 2) << 16 | (Uint32)v << 8 | 0xFF;
            for (int x = 0; x < SCREEN_W; x++) {
                port->framebuffer[s][y * SCREEN_W + x] = c;
            }
        }
    }
    if (have_icon) {
        const int scale = 4, ox = (SCREEN_W - 32 * scale) / 2, oy = (SCREEN_H - 32 * scale) / 2;
        for (int y = 0; y < 32 * scale; y++) {
            for (int x = 0; x < 32 * scale; x++) {
                Uint32 p = icon[(y / scale) * 32 + x / scale];
                if (p) {
                    port->framebuffer[0][(oy + y) * SCREEN_W + ox + x] = p;
                }
            }
        }
    }
}

static void present(Port *port)
{
    int w, h;
    SDL_GetCurrentRenderOutputSize(port->renderer, &w, &h);
    int ww;
    SDL_GetWindowSize(port->window, &ww, NULL);
    float gap = SCREEN_GAP * (ww > 0 ? (float)w / ww : 1.0f);
    /* largest scale that fits both screens stacked; integer when there is room, for crisp pixels */
    float scale = SDL_min(w / (float)SCREEN_W, (h - gap) / (float)(SCREEN_H * 2));
    if (scale >= 2) {
        scale = SDL_floorf(scale);
    }
    float sw = SCREEN_W * scale, sh = SCREEN_H * scale;
    float x = (w - sw) / 2, y = (h - (sh * 2 + gap)) / 2;

    SDL_SetRenderDrawColor(port->renderer, 0, 0, 0, 255);
    SDL_RenderClear(port->renderer);
    for (int s = 0; s < 2; s++) {
        SDL_UpdateTexture(port->screens[s], NULL, port->framebuffer[s], SCREEN_W * 4);
        SDL_FRect dst = { x, y + s * (sh + gap), sw, sh };
        SDL_RenderTexture(port->renderer, port->screens[s], NULL, &dst);
    }
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    SDL_SetAppMetadata("Castlevania: Order of Ecclesia", "0.1", "com.castlevania.collection.ooe");
    const char *rom_path = NULL, *save_dir = NULL, *screenshot = NULL;
    if (!parse_args(argc, argv, &rom_path, &save_dir, &screenshot)) {
        fail("Uso: ooe --rom <arquivo .nds> [--save-dir <pasta>]\nAbra o jogo pelo Castlevania Collection.");
        return SDL_APP_FAILURE;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        fail("SDL_Init: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    Port *port = SDL_calloc(1, sizeof(Port));
    *appstate = port;
    port->screenshot_path = screenshot;

    if (!rom_load(&port->rom, rom_path)) {
        fail("Não consegui abrir a ROM: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    if (SDL_strcmp(port->rom.game_code, "YR9E") != 0) {
        fail("Esta ROM é %s (%s); o port precisa de Order of Ecclesia USA (YR9E).", port->rom.title, port->rom.game_code);
        return SDL_APP_FAILURE;
    }
    if (save_dir) {
        port->save_dir = SDL_strdup(save_dir);
    } else {
        port->save_dir = SDL_GetPrefPath("CastlevaniaCollection", "saves/ooe");
    }
    SDL_CreateDirectory(port->save_dir);
    SDL_Log("ROM %s [%s]: ARM9 %u bytes @ %08X, %d overlays; saves in %s", port->rom.title, port->rom.game_code,
            port->rom.arm9_size, port->rom.arm9_ram, port->rom.overlay_count, port->save_dir);

    if (!SDL_CreateWindowAndRenderer("Castlevania: Order of Ecclesia", SCREEN_W * 2, SCREEN_H * 4 + SCREEN_GAP,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &port->window,
                                     &port->renderer)) {
        fail("Janela: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderVSync(port->renderer, 1);
    for (int s = 0; s < 2; s++) {
        port->screens[s] = SDL_CreateTexture(port->renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING,
                                             SCREEN_W, SCREEN_H);
        SDL_SetTextureScaleMode(port->screens[s], SDL_SCALEMODE_NEAREST);
    }
    draw_placeholder(port);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    (void)appstate;
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE) {
        return SDL_APP_SUCCESS; /* back to the launcher */
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    Port *port = appstate;
    present(port);
    if (port->screenshot_path && ++port->frames == 10) {
        SDL_Surface *shot = SDL_RenderReadPixels(port->renderer, NULL);
        bool ok = shot && SDL_SaveBMP(shot, port->screenshot_path);
        SDL_DestroySurface(shot);
        return ok ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }
    SDL_RenderPresent(port->renderer);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    (void)result;
    Port *port = appstate;
    if (!port) {
        return;
    }
    for (int s = 0; s < 2; s++) {
        SDL_DestroyTexture(port->screens[s]);
    }
    rom_free(&port->rom);
    SDL_free(port->save_dir);
    SDL_free(port);
}
