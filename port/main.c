/* Order of Ecclesia for Windows and macOS.
 *
 * Launched by the Castlevania Collection launcher as:
 *   ooe --rom <the user's .nds dump> --save-dir <folder for saves, e.g. inside Google Drive>
 *
 * The game runs on a DS core loaded through the libretro API (melonDS DS by default, from
 * cores/ next to this executable). This file is the host: window, both screens, audio,
 * keyboard/gamepad/mouse input, and saves written into --save-dir. */

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "libretro.h"
#include "rom.h"

#define MAX_OPTIONS 256

#if defined(SDL_PLATFORM_WINDOWS)
#define PATH_SEP '\\'
#else
#define PATH_SEP '/'
#endif
#define SRAM_FLUSH_MS 3000

typedef struct CoreApi {
    SDL_SharedObject *lib;
    void (*init)(void);
    void (*deinit)(void);
    void (*get_system_info)(struct retro_system_info *);
    void (*get_system_av_info)(struct retro_system_av_info *);
    void (*set_environment)(retro_environment_t);
    void (*set_video_refresh)(retro_video_refresh_t);
    void (*set_audio_sample)(retro_audio_sample_t);
    void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*set_input_poll)(retro_input_poll_t);
    void (*set_input_state)(retro_input_state_t);
    void (*set_controller_port_device)(unsigned, unsigned);
    void (*run)(void);
    bool (*load_game)(const struct retro_game_info *);
    void (*unload_game)(void);
    void *(*get_memory_data)(unsigned);
    size_t (*get_memory_size)(unsigned);
    size_t (*serialize_size)(void);
    bool (*serialize)(void *, size_t);
    bool (*unserialize)(const void *, size_t);
} CoreApi;

typedef struct Option {
    char *key;
    char *value;
} Option;

typedef struct Host {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *frame;
    int frame_w, frame_h;
    SDL_PixelFormat core_format;
    SDL_FRect frame_rect; /* where the core's picture is drawn, in window pixels */
    SDL_AudioStream *audio;
    SDL_Gamepad *gamepad;

    CoreApi core;
    struct retro_system_av_info av;
    Option options[MAX_OPTIONS];
    int option_count;
    bool options_changed;

    NdsRom rom;
    char *rom_path;
    char *save_dir;
    char *system_dir;
    char *sram_path;
    Uint8 *sram_shadow; /* last written copy, to only write when it changed */
    size_t sram_shadow_size;
    Uint64 sram_last_check;

    Uint64 next_frame_ns;
    double frame_ns;
    bool mouse_down;
    float mouse_x, mouse_y;
    char toast[128];
    Uint64 toast_until;

    /* test mode: --test-frames N [--script file] [--screenshot out.bmp] runs N frames unthrottled,
     * pressing buttons from the script ("<frame> <buttons>" lines, e.g. "600 start"), then quits */
    int test_frames, frame_count;
    const char *screenshot_path;
    struct ScriptStep {
        int frame;
        int16_t bits;
    } *script;
    int script_len;
    int16_t script_bits;
} Host;

static Host *g_host; /* libretro callbacks carry no user pointer */

static void fail(const char *fmt, ...)
{
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    SDL_Log("%s", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Order of Ecclesia", msg, NULL);
}

static void toast(Host *h, const char *msg)
{
    SDL_strlcpy(h->toast, msg, sizeof(h->toast));
    h->toast_until = SDL_GetTicks() + 2000;
    SDL_Log("%s", msg);
}

/* ---- core options ------------------------------------------------------------------ */

/* Overrides applied on top of the core's defaults. */
static const struct {
    const char *key;
    const char *value;
} option_overrides[] = {
    { "melonds_console_mode", "ds" },
    { "melonds_sysfile_mode", "builtin" },
    { "melonds_boot_mode", "direct" },
    { "melonds_render_mode", "software" },
    { "melonds_screen_layout1", "top-bottom" },
    { "melonds_number_of_screen_layouts", "1" },
    { "melonds_touch_mode", "pointer" },
    { "melonds_show_cursor", "touching" },
};

static void set_option(Host *h, const char *key, const char *value)
{
    for (int i = 0; i < h->option_count; i++) {
        if (SDL_strcmp(h->options[i].key, key) == 0) {
            SDL_free(h->options[i].value);
            h->options[i].value = SDL_strdup(value);
            return;
        }
    }
    if (h->option_count < MAX_OPTIONS) {
        h->options[h->option_count++] = (Option){ SDL_strdup(key), SDL_strdup(value) };
    }
}

static void define_option(Host *h, const char *key, const char *default_value, const struct retro_core_option_value *values)
{
    const char *value = default_value;
    for (size_t i = 0; i < SDL_arraysize(option_overrides); i++) {
        if (SDL_strcmp(option_overrides[i].key, key) != 0) {
            continue;
        }
        /* only use an override the core actually offers */
        for (int v = 0; values && v < RETRO_NUM_CORE_OPTION_VALUES_MAX && values[v].value; v++) {
            if (SDL_strcmp(values[v].value, option_overrides[i].value) == 0) {
                value = option_overrides[i].value;
            }
        }
    }
    if (!value && values && values[0].value) {
        value = values[0].value;
    }
    set_option(h, key, value ? value : "");
}

static const char *get_option(Host *h, const char *key)
{
    for (int i = 0; i < h->option_count; i++) {
        if (SDL_strcmp(h->options[i].key, key) == 0) {
            return h->options[i].value;
        }
    }
    return NULL;
}

/* ---- libretro callbacks ------------------------------------------------------------ */

static void RETRO_CALLCONV core_log(enum retro_log_level level, const char *fmt, ...)
{
    if (level < RETRO_LOG_WARN) {
        return;
    }
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    size_t n = SDL_strlen(msg);
    if (n && msg[n - 1] == '\n') {
        msg[n - 1] = '\0';
    }
    SDL_Log("[core] %s", msg);
}

static bool RETRO_CALLCONV environment(unsigned cmd, void *data)
{
    Host *h = g_host;
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool *)data = true;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        enum retro_pixel_format f = *(enum retro_pixel_format *)data;
        if (f == RETRO_PIXEL_FORMAT_XRGB8888) {
            h->core_format = SDL_PIXELFORMAT_XRGB8888;
        } else if (f == RETRO_PIXEL_FORMAT_RGB565) {
            h->core_format = SDL_PIXELFORMAT_RGB565;
        } else {
            return false;
        }
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
        *(const char **)data = h->system_dir;
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = h->save_dir;
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = core_log;
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *(unsigned *)data = 2;
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL: {
        const struct retro_core_options_v2 *opts =
            cmd == RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2 ? data : ((const struct retro_core_options_v2_intl *)data)->us;
        for (const struct retro_core_option_v2_definition *d = opts ? opts->definitions : NULL; d && d->key; d++) {
            define_option(h, d->key, d->default_value, d->values);
        }
        return true;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL: {
        const struct retro_core_option_definition *d =
            cmd == RETRO_ENVIRONMENT_SET_CORE_OPTIONS ? data : ((const struct retro_core_options_intl *)data)->us;
        for (; d && d->key; d++) {
            define_option(h, d->key, d->default_value, d->values);
        }
        return true;
    }
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        for (const struct retro_variable *v = data; v && v->key; v++) {
            /* "Description; first|second|..." — the first choice is the default */
            const char *choices = SDL_strchr(v->value, ';');
            char first[128] = "";
            if (choices) {
                choices++;
                while (*choices == ' ') {
                    choices++;
                }
                SDL_strlcpy(first, choices, sizeof(first));
                char *bar = SDL_strchr(first, '|');
                if (bar) {
                    *bar = '\0';
                }
            }
            define_option(h, v->key, first, NULL);
        }
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        v->value = get_option(h, v->key);
        return v->value != NULL;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = h->options_changed;
        h->options_changed = false;
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
    case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE:
        return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
        h->av.geometry = *(const struct retro_game_geometry *)data;
        return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        h->av = *(const struct retro_system_av_info *)data;
        h->frame_ns = 1e9 / h->av.timing.fps;
        return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
        *(unsigned *)data = RETRO_LANGUAGE_ENGLISH;
        return true;
    case RETRO_ENVIRONMENT_GET_USERNAME:
        *(const char **)data = "Shanoa";
        return true;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
        *(int *)data = 3;
        return true;
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
        *(unsigned *)data = RETRO_HW_CONTEXT_NONE;
        return true;
    case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES:
        *(uint64_t *)data = (1u << RETRO_DEVICE_JOYPAD) | (1u << RETRO_DEVICE_POINTER);
        return true;
    case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
        *(unsigned *)data = 1;
        return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE:
        SDL_Log("[core] %s", ((const struct retro_message *)data)->msg);
        return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
        SDL_Log("[core] %s", ((const struct retro_message_ext *)data)->msg);
        return true;
    case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
        *(bool *)data = false;
        return true;
    case RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE:
        *(float *)data = 60.0f;
        return true;
    default:
        return false;
    }
}

static void RETRO_CALLCONV video_refresh(const void *data, unsigned width, unsigned height, size_t pitch)
{
    Host *h = g_host;
    if (!data) {
        return; /* duplicate frame: keep the texture as it is */
    }
    if (!h->frame || (int)width != h->frame_w || (int)height != h->frame_h) {
        SDL_DestroyTexture(h->frame);
        h->frame = SDL_CreateTexture(h->renderer, h->core_format, SDL_TEXTUREACCESS_STREAMING, (int)width, (int)height);
        SDL_SetTextureScaleMode(h->frame, SDL_SCALEMODE_NEAREST);
        h->frame_w = (int)width;
        h->frame_h = (int)height;
    }
    SDL_UpdateTexture(h->frame, NULL, data, (int)pitch);
}

static void RETRO_CALLCONV audio_sample(int16_t left, int16_t right)
{
    int16_t s[2] = { left, right };
    SDL_PutAudioStreamData(g_host->audio, s, sizeof(s));
}

static size_t RETRO_CALLCONV audio_batch(const int16_t *data, size_t frames)
{
    SDL_PutAudioStreamData(g_host->audio, data, (int)(frames * 4));
    return frames;
}

static void RETRO_CALLCONV input_poll(void)
{
}

/* keyboard: arrows, X = A, Z = B, S = X, A = Y, Q = L, W = R, Enter = Start, Shift = Select */
static int16_t joypad_bits(Host *h)
{
    const bool *k = SDL_GetKeyboardState(NULL);
    int16_t bits = 0;
#define KEY(sc, id) if (k[sc]) bits |= (int16_t)(1 << (id))
    KEY(SDL_SCANCODE_UP, RETRO_DEVICE_ID_JOYPAD_UP);
    KEY(SDL_SCANCODE_DOWN, RETRO_DEVICE_ID_JOYPAD_DOWN);
    KEY(SDL_SCANCODE_LEFT, RETRO_DEVICE_ID_JOYPAD_LEFT);
    KEY(SDL_SCANCODE_RIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT);
    KEY(SDL_SCANCODE_X, RETRO_DEVICE_ID_JOYPAD_A);
    KEY(SDL_SCANCODE_Z, RETRO_DEVICE_ID_JOYPAD_B);
    KEY(SDL_SCANCODE_S, RETRO_DEVICE_ID_JOYPAD_X);
    KEY(SDL_SCANCODE_A, RETRO_DEVICE_ID_JOYPAD_Y);
    KEY(SDL_SCANCODE_Q, RETRO_DEVICE_ID_JOYPAD_L);
    KEY(SDL_SCANCODE_W, RETRO_DEVICE_ID_JOYPAD_R);
    KEY(SDL_SCANCODE_RETURN, RETRO_DEVICE_ID_JOYPAD_START);
    KEY(SDL_SCANCODE_RSHIFT, RETRO_DEVICE_ID_JOYPAD_SELECT);
    KEY(SDL_SCANCODE_LSHIFT, RETRO_DEVICE_ID_JOYPAD_SELECT);
#undef KEY
    SDL_Gamepad *g = h->gamepad;
    if (g) {
        /* Nintendo-style positions: the bottom face button is B, the right one is A */
#define PAD(b, id) if (SDL_GetGamepadButton(g, b)) bits |= (int16_t)(1 << (id))
        PAD(SDL_GAMEPAD_BUTTON_DPAD_UP, RETRO_DEVICE_ID_JOYPAD_UP);
        PAD(SDL_GAMEPAD_BUTTON_DPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_DOWN);
        PAD(SDL_GAMEPAD_BUTTON_DPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_LEFT);
        PAD(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT);
        PAD(SDL_GAMEPAD_BUTTON_SOUTH, RETRO_DEVICE_ID_JOYPAD_B);
        PAD(SDL_GAMEPAD_BUTTON_EAST, RETRO_DEVICE_ID_JOYPAD_A);
        PAD(SDL_GAMEPAD_BUTTON_WEST, RETRO_DEVICE_ID_JOYPAD_Y);
        PAD(SDL_GAMEPAD_BUTTON_NORTH, RETRO_DEVICE_ID_JOYPAD_X);
        PAD(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, RETRO_DEVICE_ID_JOYPAD_L);
        PAD(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, RETRO_DEVICE_ID_JOYPAD_R);
        PAD(SDL_GAMEPAD_BUTTON_START, RETRO_DEVICE_ID_JOYPAD_START);
        PAD(SDL_GAMEPAD_BUTTON_BACK, RETRO_DEVICE_ID_JOYPAD_SELECT);
#undef PAD
        const Sint16 dead = 12000;
        Sint16 ax = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX), ay = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
        if (ax < -dead) bits |= 1 << RETRO_DEVICE_ID_JOYPAD_LEFT;
        if (ax > dead) bits |= 1 << RETRO_DEVICE_ID_JOYPAD_RIGHT;
        if (ay < -dead) bits |= 1 << RETRO_DEVICE_ID_JOYPAD_UP;
        if (ay > dead) bits |= 1 << RETRO_DEVICE_ID_JOYPAD_DOWN;
        if (SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000) bits |= 1 << RETRO_DEVICE_ID_JOYPAD_L;
        if (SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000) bits |= 1 << RETRO_DEVICE_ID_JOYPAD_R;
    }
    return bits;
}

static int16_t RETRO_CALLCONV input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    Host *h = g_host;
    (void)index;
    if (port != 0) {
        return 0;
    }
    if (device == RETRO_DEVICE_JOYPAD) {
        int16_t bits = h->test_frames ? h->script_bits : joypad_bits(h);
        return id == RETRO_DEVICE_ID_JOYPAD_MASK ? bits : (int16_t)((bits >> id) & 1);
    }
    if (device == RETRO_DEVICE_POINTER) {
        SDL_FRect r = h->frame_rect;
        bool inside = r.w > 0 && h->mouse_x >= r.x && h->mouse_x < r.x + r.w && h->mouse_y >= r.y && h->mouse_y < r.y + r.h;
        switch (id) {
        case RETRO_DEVICE_ID_POINTER_X:
            return inside ? (int16_t)((h->mouse_x - r.x) / r.w * 0xFFFE - 0x7FFF) : -0x8000;
        case RETRO_DEVICE_ID_POINTER_Y:
            return inside ? (int16_t)((h->mouse_y - r.y) / r.h * 0xFFFE - 0x7FFF) : -0x8000;
        case RETRO_DEVICE_ID_POINTER_PRESSED:
            return inside && h->mouse_down;
        case RETRO_DEVICE_ID_POINTER_IS_OFFSCREEN:
            return !inside;
        case RETRO_DEVICE_ID_POINTER_COUNT:
            return 1;
        default:
            return 0;
        }
    }
    return 0;
}

/* ---- core loading ------------------------------------------------------------------ */

static bool load_core(Host *h, const char *path)
{
    CoreApi *c = &h->core;
    c->lib = SDL_LoadObject(path);
    if (!c->lib) {
        return false;
    }
#define SYM(field, name) \
    if (!(*(void **)&c->field = (void *)SDL_LoadFunction(c->lib, name))) { \
        SDL_SetError("core is missing %s", name); \
        return false; \
    }
    SYM(init, "retro_init");
    SYM(deinit, "retro_deinit");
    SYM(get_system_info, "retro_get_system_info");
    SYM(get_system_av_info, "retro_get_system_av_info");
    SYM(set_environment, "retro_set_environment");
    SYM(set_video_refresh, "retro_set_video_refresh");
    SYM(set_audio_sample, "retro_set_audio_sample");
    SYM(set_audio_sample_batch, "retro_set_audio_sample_batch");
    SYM(set_input_poll, "retro_set_input_poll");
    SYM(set_input_state, "retro_set_input_state");
    SYM(set_controller_port_device, "retro_set_controller_port_device");
    SYM(run, "retro_run");
    SYM(load_game, "retro_load_game");
    SYM(unload_game, "retro_unload_game");
    SYM(get_memory_data, "retro_get_memory_data");
    SYM(get_memory_size, "retro_get_memory_size");
    SYM(serialize_size, "retro_serialize_size");
    SYM(serialize, "retro_serialize");
    SYM(unserialize, "retro_unserialize");
#undef SYM
    return true;
}

static char *find_core(void)
{
    static const char *names[] = { "melondsds_libretro", "melonds_libretro", "desmume_libretro" };
#if defined(SDL_PLATFORM_WINDOWS)
    const char *ext = "dll";
#elif defined(SDL_PLATFORM_APPLE)
    const char *ext = "dylib";
#else
    const char *ext = "so";
#endif
    const char *base = SDL_GetBasePath();
    for (size_t i = 0; i < SDL_arraysize(names); i++) {
        char *path;
        SDL_asprintf(&path, "%scores%c%s.%s", base, PATH_SEP, names[i], ext);
        SDL_PathInfo info;
        if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE) {
            return path;
        }
        SDL_free(path);
    }
    return NULL;
}

/* ---- saves ------------------------------------------------------------------------- */

/* Writes through a temp file + rename so a sync client never sees half a save. */
static bool write_atomic(const char *path, const void *data, size_t size)
{
    char *tmp;
    SDL_asprintf(&tmp, "%s.tmp", path);
    bool ok = SDL_SaveFile(tmp, data, size) && SDL_RenamePath(tmp, path);
    if (!ok) {
        SDL_Log("could not write %s: %s", path, SDL_GetError());
        SDL_RemovePath(tmp);
    }
    SDL_free(tmp);
    return ok;
}

static void sram_load(Host *h)
{
    size_t size = h->core.get_memory_size(RETRO_MEMORY_SAVE_RAM);
    void *mem = h->core.get_memory_data(RETRO_MEMORY_SAVE_RAM);
    if (!size || !mem) {
        return;
    }
    size_t file_size;
    void *data = SDL_LoadFile(h->sram_path, &file_size);
    if (data) {
        SDL_memcpy(mem, data, SDL_min(size, file_size));
        SDL_free(data);
        SDL_Log("save loaded from %s", h->sram_path);
    }
    h->sram_shadow = SDL_malloc(size);
    SDL_memcpy(h->sram_shadow, mem, size);
    h->sram_shadow_size = size;
}

static void sram_flush(Host *h, bool force)
{
    size_t size = h->core.get_memory_size(RETRO_MEMORY_SAVE_RAM);
    void *mem = h->core.get_memory_data(RETRO_MEMORY_SAVE_RAM);
    if (!size || !mem) {
        return;
    }
    if (!force && h->sram_shadow && h->sram_shadow_size == size && SDL_memcmp(h->sram_shadow, mem, size) == 0) {
        return;
    }
    if (write_atomic(h->sram_path, mem, size)) {
        if (h->sram_shadow_size != size) {
            SDL_free(h->sram_shadow);
            h->sram_shadow = SDL_malloc(size);
            h->sram_shadow_size = size;
        }
        SDL_memcpy(h->sram_shadow, mem, size);
    }
}

static void state_path(Host *h, char *out, size_t n)
{
    SDL_snprintf(out, n, "%s%cooe.state", h->save_dir, PATH_SEP);
}

static void save_state(Host *h)
{
    size_t size = h->core.serialize_size();
    void *buf = size ? SDL_malloc(size) : NULL;
    char path[1024];
    state_path(h, path, sizeof(path));
    if (buf && h->core.serialize(buf, size) && write_atomic(path, buf, size)) {
        toast(h, "Estado salvo (F9 para voltar a ele)");
    } else {
        toast(h, "Não consegui salvar o estado");
    }
    SDL_free(buf);
}

static void load_state(Host *h)
{
    char path[1024];
    state_path(h, path, sizeof(path));
    size_t size;
    void *buf = SDL_LoadFile(path, &size);
    if (buf && h->core.unserialize(buf, size)) {
        toast(h, "Estado carregado");
    } else {
        toast(h, "Nenhum estado salvo ainda (F5 salva)");
    }
    SDL_free(buf);
}

/* ---- presentation ------------------------------------------------------------------ */

static void present(Host *h)
{
    int w, hgt;
    SDL_GetCurrentRenderOutputSize(h->renderer, &w, &hgt);
    SDL_SetRenderDrawColor(h->renderer, 0, 0, 0, 255);
    SDL_RenderClear(h->renderer);
    if (h->frame) {
        float sx = (float)w / h->frame_w, sy = (float)hgt / h->frame_h;
        float scale = SDL_min(sx, sy);
        if (scale >= 2) {
            scale = SDL_floorf(scale); /* crisp pixels when there is room */
        }
        float fw = h->frame_w * scale, fh = h->frame_h * scale;
        h->frame_rect = (SDL_FRect){ SDL_floorf((w - fw) / 2), SDL_floorf((hgt - fh) / 2), fw, fh };
        SDL_RenderTexture(h->renderer, h->frame, NULL, &h->frame_rect);
    }
    if (SDL_GetTicks() < h->toast_until) {
        float scale = 2.0f;
        SDL_SetRenderScale(h->renderer, scale, scale);
        SDL_SetRenderDrawColor(h->renderer, 0, 0, 0, 200);
        SDL_SetRenderDrawBlendMode(h->renderer, SDL_BLENDMODE_BLEND);
        float tw = (float)SDL_strlen(h->toast) * 8 + 12;
        SDL_RenderFillRect(h->renderer, &(SDL_FRect){ 6, 6, tw, 18 });
        SDL_SetRenderDrawColor(h->renderer, 255, 255, 255, 255);
        SDL_RenderDebugText(h->renderer, 12, 11, h->toast);
        SDL_SetRenderScale(h->renderer, 1, 1);
    }
    if (!h->test_frames) {
        SDL_RenderPresent(h->renderer); /* test mode reads the back buffer before presenting */
    }
}

/* ---- test mode --------------------------------------------------------------------- */

static int16_t parse_buttons(const char *s)
{
    static const struct {
        const char *name;
        int id;
    } names[] = {
        { "a", RETRO_DEVICE_ID_JOYPAD_A }, { "b", RETRO_DEVICE_ID_JOYPAD_B }, { "x", RETRO_DEVICE_ID_JOYPAD_X },
        { "y", RETRO_DEVICE_ID_JOYPAD_Y }, { "l", RETRO_DEVICE_ID_JOYPAD_L }, { "r", RETRO_DEVICE_ID_JOYPAD_R },
        { "start", RETRO_DEVICE_ID_JOYPAD_START }, { "select", RETRO_DEVICE_ID_JOYPAD_SELECT },
        { "up", RETRO_DEVICE_ID_JOYPAD_UP }, { "down", RETRO_DEVICE_ID_JOYPAD_DOWN },
        { "left", RETRO_DEVICE_ID_JOYPAD_LEFT }, { "right", RETRO_DEVICE_ID_JOYPAD_RIGHT },
    };
    int16_t bits = 0;
    char buf[256];
    SDL_strlcpy(buf, s, sizeof(buf));
    char *save = NULL;
    for (char *tok = SDL_strtok_r(buf, " +,\t\r\n", &save); tok; tok = SDL_strtok_r(NULL, " +,\t\r\n", &save)) {
        for (size_t i = 0; i < SDL_arraysize(names); i++) {
            if (SDL_strcasecmp(tok, names[i].name) == 0) {
                bits |= (int16_t)(1 << names[i].id);
            }
        }
    }
    return bits;
}

static void load_script(Host *h, const char *path)
{
    char *text = SDL_LoadFile(path, NULL);
    if (!text) {
        SDL_Log("script %s: %s", path, SDL_GetError());
        return;
    }
    char *save = NULL;
    for (char *line = SDL_strtok_r(text, "\n", &save); line; line = SDL_strtok_r(NULL, "\n", &save)) {
        char *end;
        long frame = SDL_strtol(line, &end, 10);
        if (end == line || *line == '#') {
            continue;
        }
        h->script = SDL_realloc(h->script, sizeof(*h->script) * (size_t)(h->script_len + 1));
        h->script[h->script_len++] = (struct ScriptStep){ (int)frame, parse_buttons(end) };
    }
    SDL_free(text);
}

/* Runs the test unthrottled, a batch of frames per iteration; quits when done. */
static SDL_AppResult run_test(Host *h)
{
    for (int batch = 0; batch < 120 && h->frame_count < h->test_frames; batch++) {
        for (int i = 0; i < h->script_len; i++) {
            if (h->script[i].frame == h->frame_count) {
                h->script_bits = h->script[i].bits;
            }
        }
        h->core.run();
        h->frame_count++;
    }
    present(h);
    if (h->frame_count < h->test_frames) {
        return SDL_APP_CONTINUE;
    }
    if (h->screenshot_path) {
        SDL_Surface *shot = SDL_RenderReadPixels(h->renderer, NULL);
        if (!shot || !SDL_SaveBMP(shot, h->screenshot_path)) {
            SDL_Log("screenshot: %s", SDL_GetError());
        }
        SDL_DestroySurface(shot);
    }
    return SDL_APP_SUCCESS;
}

/* ---- SDL app callbacks ------------------------------------------------------------- */

static const char *arg_value(int argc, char **argv, const char *name)
{
    for (int i = 1; i + 1 < argc; i++) {
        if (SDL_strcmp(argv[i], name) == 0) {
            return argv[i + 1];
        }
    }
    return NULL;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    SDL_SetAppMetadata("Castlevania: Order of Ecclesia", "1.0", "com.castlevania.collection.ooe");
    const char *rom_path = arg_value(argc, argv, "--rom");
    if (!rom_path) {
        fail("Uso: ooe --rom <arquivo .nds> [--save-dir <pasta>]\nAbra o jogo pelo Castlevania Collection.");
        return SDL_APP_FAILURE;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        fail("SDL_Init: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    Host *h = SDL_calloc(1, sizeof(Host));
    *appstate = g_host = h;
    h->core_format = SDL_PIXELFORMAT_XRGB1555;

    if (!rom_load(&h->rom, rom_path)) {
        fail("Não consegui abrir a ROM: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    if (SDL_strcmp(h->rom.game_code, "YR9E") != 0) {
        fail("Esta ROM é %s (%s); o port precisa de Order of Ecclesia USA (YR9E).", h->rom.title, h->rom.game_code);
        return SDL_APP_FAILURE;
    }
    h->rom_path = SDL_strdup(rom_path);

    const char *save_dir = arg_value(argc, argv, "--save-dir");
    h->save_dir = save_dir ? SDL_strdup(save_dir) : SDL_GetPrefPath("CastlevaniaCollection", "saves/ooe");
    size_t n = SDL_strlen(h->save_dir);
    if (n > 1 && (h->save_dir[n - 1] == '/' || h->save_dir[n - 1] == '\\')) {
        h->save_dir[n - 1] = '\0';
    }
    SDL_CreateDirectory(h->save_dir);
    SDL_asprintf(&h->sram_path, "%s%cooe.sav", h->save_dir, PATH_SEP);
    char *pref = SDL_GetPrefPath("CastlevaniaCollection", "system");
    h->system_dir = pref ? pref : SDL_strdup(h->save_dir);

    char *core_path = find_core();
    if (!core_path || !load_core(h, core_path)) {
        fail("Não encontrei o núcleo de DS em cores/ ao lado do executável (%s).\n"
             "Rode tools/fetch_core para baixá-lo.", core_path ? SDL_GetError() : "nenhum arquivo");
        SDL_free(core_path);
        return SDL_APP_FAILURE;
    }
    SDL_Log("core: %s", core_path);
    SDL_free(core_path);

    CoreApi *c = &h->core;
    c->set_environment(environment);
    c->set_video_refresh(video_refresh);
    c->set_audio_sample(audio_sample);
    c->set_audio_sample_batch(audio_batch);
    c->set_input_poll(input_poll);
    c->set_input_state(input_state);
    c->init();

    struct retro_game_info game = { h->rom_path, h->rom.data, h->rom.size, NULL };
    if (!c->load_game(&game)) {
        fail("O núcleo não conseguiu carregar a ROM.");
        return SDL_APP_FAILURE;
    }
    c->set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
    c->get_system_av_info(&h->av);
    h->frame_ns = 1e9 / (h->av.timing.fps > 0 ? h->av.timing.fps : 59.8261);
    sram_load(h);
    if (SDL_getenv("OOE_DUMP_OPTIONS")) {
        for (int i = 0; i < h->option_count; i++) {
            SDL_Log("option %s = %s", h->options[i].key, h->options[i].value);
        }
    }

    int base_w = h->av.geometry.base_width ? (int)h->av.geometry.base_width : 256;
    int base_h = h->av.geometry.base_height ? (int)h->av.geometry.base_height : 384;
    if (!SDL_CreateWindowAndRenderer("Castlevania: Order of Ecclesia", base_w * 2, base_h * 2,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &h->window, &h->renderer)) {
        fail("Janela: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderVSync(h->renderer, 1);

    SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, (int)(h->av.timing.sample_rate > 0 ? h->av.timing.sample_rate : 32768) };
    h->audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (h->audio) {
        SDL_ResumeAudioStreamDevice(h->audio);
    } else {
        SDL_Log("audio: %s", SDL_GetError());
    }
    const char *frames = arg_value(argc, argv, "--test-frames");
    if (frames) {
        h->test_frames = SDL_atoi(frames);
        h->screenshot_path = arg_value(argc, argv, "--screenshot");
        const char *script = arg_value(argc, argv, "--script");
        if (script) {
            load_script(h, script);
        }
    }
    h->next_frame_ns = SDL_GetTicksNS();
    h->sram_last_check = SDL_GetTicks();
    toast(h, "F5 salvar estado  F9 carregar  F11 tela cheia  Esc sair");
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    Host *h = appstate;
    switch (event->type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;
    case SDL_EVENT_KEY_DOWN:
        if (event->key.repeat) {
            break;
        }
        switch (event->key.key) {
        case SDLK_ESCAPE:
            return SDL_APP_SUCCESS;
        case SDLK_F5:
            save_state(h);
            break;
        case SDLK_F9:
            load_state(h);
            break;
        case SDLK_F11:
            SDL_SetWindowFullscreen(h->window, !(SDL_GetWindowFlags(h->window) & SDL_WINDOW_FULLSCREEN));
            break;
        case SDLK_RETURN:
            if (event->key.mod & SDL_KMOD_ALT) {
                SDL_SetWindowFullscreen(h->window, !(SDL_GetWindowFlags(h->window) & SDL_WINDOW_FULLSCREEN));
            }
            break;
        default:
            break;
        }
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        if (!h->gamepad) {
            h->gamepad = SDL_OpenGamepad(event->gdevice.which);
        }
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        if (h->gamepad && SDL_GetGamepadID(h->gamepad) == event->gdevice.which) {
            SDL_CloseGamepad(h->gamepad);
            h->gamepad = NULL;
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        SDL_ConvertEventToRenderCoordinates(h->renderer, event);
        if (event->type == SDL_EVENT_MOUSE_MOTION) {
            h->mouse_x = event->motion.x;
            h->mouse_y = event->motion.y;
        } else if (event->button.button == SDL_BUTTON_LEFT) {
            h->mouse_x = event->button.x;
            h->mouse_y = event->button.y;
            h->mouse_down = event->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        }
        break;
    }
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
        sram_flush(h, false);
        break;
    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    Host *h = appstate;
    if (h->test_frames) {
        return run_test(h);
    }
    Uint64 now = SDL_GetTicksNS();
    /* run as many emulated frames as real time asks for (at most a few, after a stall) */
    int runs = 0;
    while (now >= h->next_frame_ns && runs < 4) {
        h->core.run();
        h->next_frame_ns += (Uint64)h->frame_ns;
        runs++;
    }
    if (now > h->next_frame_ns + (Uint64)(h->frame_ns * 8)) {
        h->next_frame_ns = now; /* fell far behind (window dragged, debugger): resync */
    }
    /* keep audio latency bounded */
    if (h->audio && SDL_GetAudioStreamQueued(h->audio) > (int)(h->av.timing.sample_rate * 4 / 5)) {
        SDL_ClearAudioStream(h->audio);
    }
    present(h);
    if (SDL_GetTicks() - h->sram_last_check > SRAM_FLUSH_MS) {
        sram_flush(h, false);
        h->sram_last_check = SDL_GetTicks();
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    (void)result;
    Host *h = appstate;
    if (!h) {
        return;
    }
    if (h->core.lib && h->core.run) {
        sram_flush(h, false);
        h->core.unload_game();
        h->core.deinit();
        SDL_UnloadObject(h->core.lib);
    }
    SDL_DestroyAudioStream(h->audio);
    SDL_DestroyTexture(h->frame);
    for (int i = 0; i < h->option_count; i++) {
        SDL_free(h->options[i].key);
        SDL_free(h->options[i].value);
    }
    rom_free(&h->rom);
    SDL_free(h->sram_shadow);
    SDL_free(h->script);
    SDL_free(h->rom_path);
    SDL_free(h->save_dir);
    SDL_free(h->system_dir);
    SDL_free(h->sram_path);
    SDL_free(h);
}
