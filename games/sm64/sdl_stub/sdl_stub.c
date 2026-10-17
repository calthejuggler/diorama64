#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
static int append_dir(char *buf, size_t cap, const char *name) {
    const size_t n = strlen(buf);
    if (snprintf(buf + n, cap - n, "/%s", name) >= (int)(cap - n)) return 0;
    mkdir(buf, 0755);
    return 1;
}
char *SDL_GetPrefPath(const char *org, const char *app) {
    const char *home = getenv("HOME");
    if (!home || !app || !*app) return NULL;
    char buf[2048];
    if (snprintf(buf, sizeof buf, "%s/Library", home) >= (int)sizeof buf) return NULL;
    mkdir(buf, 0755);
    if (!append_dir(buf, sizeof buf, "Application Support")) return NULL;
    if (org && *org && !append_dir(buf, sizeof buf, org)) return NULL;
    if (!append_dir(buf, sizeof buf, app)) return NULL;
    const size_t n = strlen(buf);
    char *out = malloc(n + 2);
    if (!out) return NULL;
    memcpy(out, buf, n); out[n] = '/'; out[n + 1] = 0;
    return out;
}
void SDL_free(void *mem) { free(mem); }
Uint64 SDL_GetPerformanceCounter(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (Uint64)t.tv_sec * 1000000000ull + (Uint64)t.tv_nsec;
}
Uint64 SDL_GetPerformanceFrequency(void) { return 1000000000ull; }
Uint32 SDL_GetMouseState(int *x, int *y) { if (x) *x = 0; if (y) *y = 0; return 0; }
Uint32 SDL_GetRelativeMouseState(int *x, int *y) { if (x) *x = 0; if (y) *y = 0; return 0; }
int SDL_SetRelativeMouseMode(SDL_bool enabled) { (void)enabled; return -1; }
SDL_Keycode SDL_GetKeyFromScancode(SDL_Scancode scancode) { (void)scancode; return 0; }
const char *SDL_GetKeyName(SDL_Keycode key) { (void)key; return ""; }
const char *SDL_GetScancodeName(SDL_Scancode scancode) { (void)scancode; return ""; }
int SDL_NumJoysticks(void) { return 0; }
const char *SDL_JoystickNameForIndex(int device_index) { (void)device_index; return NULL; }
int SDL_ShowSimpleMessageBox(Uint32 flags, const char *title, const char *message, SDL_Window *window) {
    (void)flags; (void)window;
    fprintf(stderr, "%s: %s\n", title ? title : "", message ? message : "");
    return 0;
}
int SDL_GetCurrentDisplayMode(int display_index, SDL_DisplayMode *mode) {
    (void)display_index;
    if (!mode) return -1;
    memset(mode, 0, sizeof *mode);
    mode->w = 1920; mode->h = 1080; mode->refresh_rate = 90;
    return 0;
}
