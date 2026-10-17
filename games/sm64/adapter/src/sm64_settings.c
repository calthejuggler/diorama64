#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "sm64_settings.h"
static const sm64_setting_def kDefs[] = {
    { "menu_size", "normal|large", "large" },
    { "pack_tilt", "on|off", "on" },
    { "menu_backdrop", "on|off", "on" },
    { "gameover_text", "on|off", "on" },
    { "splash_lift", "on|off", "on" },
    { "title_light", "front|room", "front" },
    { "menu_backdrop_white", "on|off", "on" },
    { "letter_front", "on|off", "on" },
    { "score_float", "on|off", "on" },
    { "intro_pip", "on|off", "on" },
    { "title_eye_level", "on|off", "on" },
    { "title_touch", "both|hands|pinch", "both" },
    { "title_eyes", "both|finger|off", "both" },
    { "pip_follow", "on|off", "on" },
    { "room_reveal", "on|off", "on" },
    { "below_table", "hide|show", "hide" },
    { "trim_level", "on|off", "on" },
    { "liquids", "on|off", "on" },
    { "water_trim", "on|off", "on" },
    { "behind_walls", "silhouette|cutaway|both|off", "silhouette" },
    { "transitions", "on|off", "on" },
    { "trans_fade", "clear|colour", "clear" },
    { "fine_textures", "on|off", "on" },
    { "score_seated", "on|off", "on" },
    { "dialog_plate", "on|off", "on" },
    { "dialog_size", "normal|large", "large" },
    { "act_star_solid", "on|off", "on" },
    { "sound", "spatial|stereo", "spatial" },
    { "audio", "on|off", "on" },
    { "reverb", "neutral|original", "neutral" },
    { "event_music", "ring|bed", "ring" },
    { "resampler", "sinc|linear", "sinc" },
};
#define N_DEFS ((int)(sizeof kDefs / sizeof kDefs[0]))
/* Append only. */
static const struct { const char *key, *old_def; int rev; } kOldDefaults[] = {
    { NULL, NULL, 0 },
};
#define SETTINGS_REV 1
static char sVal[N_DEFS][16];
static int sInit;
static pthread_mutex_t sMu = PTHREAD_MUTEX_INITIALIZER;
static int find(const char *key) { for (int i = 0; key && i < N_DEFS; i++) if (!strcmp(kDefs[i].key, key)) return i; return -1; }
static void init_locked(void) { if (!sInit) { for (int i = 0; i < N_DEFS; i++) snprintf(sVal[i], sizeof sVal[i], "%s", kDefs[i].def); sInit = 1; } }
static int choice_index(const char *choices, const char *v) {
    int idx = 0; const char *p = choices; const size_t n = strlen(v);
    while (p && *p) {
        const char *bar = strchr(p, '|'); const size_t len = bar ? (size_t)(bar - p) : strlen(p);
        if (len == n && !strncmp(p, v, n)) return idx;
        idx++; p = bar ? bar + 1 : NULL;
    }
    return -1;
}
int sm64_settings_count(void) { return N_DEFS; }
const sm64_setting_def *sm64_settings_def(int i) { return i >= 0 && i < N_DEFS ? &kDefs[i] : NULL; }
int sm64_settings_set(const char *key, const char *value, char *err, size_t cap) {
    const int i = find(key);
    if (i < 0) { if (err && cap) snprintf(err, cap, "unknown setting %s", key ? key : "(null)"); return 0; }
    if (!value || choice_index(kDefs[i].choices, value) < 0) {
        if (err && cap) snprintf(err, cap, "%s: %s (one of %s)", key, value ? value : "(null)", kDefs[i].choices);
        return 0;
    }
    pthread_mutex_lock(&sMu); init_locked(); snprintf(sVal[i], sizeof sVal[i], "%s", value); pthread_mutex_unlock(&sMu);
    return 1;
}
int sm64_settings_get(const char *key, char *buf, size_t cap) {
    const int i = find(key);
    if (i < 0 || !buf || !cap) return 0;
    pthread_mutex_lock(&sMu); init_locked(); snprintf(buf, cap, "%s", sVal[i]); pthread_mutex_unlock(&sMu);
    return 1;
}
int sm64_settings_choice(const char *key) {
    char v[16]; const int i = find(key);
    if (i < 0 || !sm64_settings_get(key, v, sizeof v)) return -1;
    return choice_index(kDefs[i].choices, v);
}
void sm64_settings_reset(void) { pthread_mutex_lock(&sMu); sInit = 0; init_locked(); pthread_mutex_unlock(&sMu); }
int sm64_settings_revision(void) { return SETTINGS_REV; }
int sm64_settings_drop_stored(const char *key, const char *stored, int stored_rev, int has_launch_override) {
    const int i = find(key);
    if (i < 0 || !stored || has_launch_override) return 0;
    if (stored_rev < 1 && !strcmp(stored, kDefs[i].def)) return 1;
    for (size_t k = 0; k < sizeof kOldDefaults / sizeof kOldDefaults[0]; k++) {
        if (kOldDefaults[k].key && stored_rev < kOldDefaults[k].rev && !strcmp(key, kOldDefaults[k].key) &&
            !strcmp(stored, kOldDefaults[k].old_def)) return 1;
    }
    return 0;
}
