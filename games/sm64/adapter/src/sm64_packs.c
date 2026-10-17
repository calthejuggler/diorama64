#include "sm64_packs.h"
#include <stdio.h>
#include <string.h>
static const char *const kPreferred[3][4] = {
    { NULL, NULL, NULL, NULL },
    { "SM64 Reloaded", "Render96 HD", NULL, NULL },
    { "Render96 Models (Balanced)", "Render96 Models", "SwagSkeleton95", NULL },
};
static const char *on_word(int kind) { return kind == SM64_PACK_TEXTURES ? "hd" : kind == SM64_PACK_MODELS ? "3d" : NULL; }
int sm64_packs_kind(int tex_files, int bin_files) {
    return bin_files > 0 ? SM64_PACK_MODELS : tex_files > 0 ? SM64_PACK_TEXTURES : SM64_PACK_NONE;
}
const char *sm64_packs_kind_name(int kind) {
    return kind == SM64_PACK_TEXTURES ? "textures" : kind == SM64_PACK_MODELS ? "models" : NULL;
}
int sm64_packs_pick(const sm64_pack_state *p, int n, int kind, const char *want) {
    if (!on_word(kind) || !p) { return -1; }
    if (want && *want) {
        for (int i = 0; i < n; i++) { if (p[i].kind == kind && p[i].name && !strcmp(p[i].name, want)) { return i; } }
    }
    for (int j = 0; kPreferred[kind][j]; j++) {
        for (int i = 0; i < n; i++) { if (p[i].kind == kind && p[i].name && !strcmp(p[i].name, kPreferred[kind][j])) { return i; } }
    }
    for (int i = 0; i < n; i++) { if (p[i].kind == kind) { return i; } }
    return -1;
}
void sm64_packs_plan(const sm64_pack_state *p, int n, int kind, int chosen, int *want_on) {
    for (int i = 0; i < n; i++) { want_on[i] = p[i].kind == kind ? (i == chosen ? 1 : 0) : -1; }
}
int sm64_packs_parse_choice(int kind, const char *s, char *name, int cap) {
    const char *on = on_word(kind);
    if (!on || !s || !name || cap <= 0) { return -1; }
    if (!strcmp(s, "original")) { name[0] = '\0'; return 0; }
    const size_t L = strlen(on);
    if (strncmp(s, on, L) != 0) { return -1; }
    if (s[L] == '\0') { name[0] = '\0'; return 1; }
    const char *v = s + L + 1;
    if (s[L] != ':' || !*v || strchr(v, '/') || !strcmp(v, ".") || !strcmp(v, "..") || strlen(v) >= (size_t)cap) { return -1; }
    memcpy(name, v, strlen(v) + 1);
    return 1;
}
int sm64_packs_choice_string(int kind, int on, const char *name, char *buf, int cap) {
    const char *w = on_word(kind);
    if (!w || !buf || cap <= 0) { return 0; }
    const int n = !on ? snprintf(buf, (size_t)cap, "original")
                : (name && *name) ? snprintf(buf, (size_t)cap, "%s:%s", w, name) : snprintf(buf, (size_t)cap, "%s", w);
    return n > 0 && n < cap;
}
int sm64_packs_trees_3d(int trees, int trees_3d) { return trees_3d > 0 && trees_3d >= trees; }
