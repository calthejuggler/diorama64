#include "sm64_ar_trees.h"
#include <math.h>
#include <stdatomic.h>
#include <string.h>

static _Atomic int sStyle = SM64_TREES_CROSSED;
void sm64_trees_set_style(int style) {
    if (style == SM64_TREES_CARD || style == SM64_TREES_CROSSED) { sStyle = style; }
}
int sm64_trees_style(void) { return sStyle; }
static const char *const kNames[] = { "card", "crossed" };
const char *sm64_trees_style_name(int style) { return style >= 0 && style < 2 ? kNames[style] : NULL; }
int sm64_trees_style_parse(const char *name) {
    for (int i = 0; name && i < 2; i++) { if (strcmp(name, kNames[i]) == 0) { return i; } }
    return -1;
}

#define STEP_RAD (M_PI / 3.0)
static void axes_at(double yaw, float r[3], float u[3], float b[3]) {
    const float s = (float)sin(yaw), c = (float)cos(yaw);
    b[0] = s; b[1] = 0.f; b[2] = c;
    u[0] = 0.f; u[1] = 1.f; u[2] = 0.f;
    r[0] = c; r[1] = 0.f; r[2] = -s;
}
void sm64_trees_fixed_axes(const float p[3], float r[3], float u[3], float b[3]) {
    const uint32_t h = (uint32_t)(int32_t)lround(p[0]) * 73856093u ^ (uint32_t)(int32_t)lround(p[2]) * 19349663u;
    axes_at((double)(h % 6000u) / 6000.0 * STEP_RAD, r, u, b);
}

typedef struct { float p[3], r[3], u[3], b[3]; } tree;
#define MAX_TREES 256
static tree sTrees[MAX_TREES];
static int sCount;
static uint32_t sGen;

void sm64_trees_add(uint32_t gen, const float p[3], const float r[3], const float u[3], const float b[3]) {
    if (gen != sGen) { sGen = gen; sCount = 0; }
    for (int i = 0; i < sCount; i++) {   /* the billboard hook runs for the current and the previous (interpolation) matrix */
        if (memcmp(sTrees[i].p, p, sizeof sTrees[i].p) == 0) { return; }
    }
    if (sCount == MAX_TREES) { return; }
    tree *t = &sTrees[sCount++];
    memcpy(t->p, p, sizeof t->p); memcpy(t->r, r, sizeof t->r); memcpy(t->u, u, sizeof t->u); memcpy(t->b, b, sizeof t->b);
}

static float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void local_of(const tree *t, const float v[3], float out[3]) {
    const float d[3] = { v[0] - t->p[0], v[1] - t->p[1], v[2] - t->p[2] };
    out[0] = dot3(d, t->r); out[1] = dot3(d, t->u); out[2] = dot3(d, t->b);
}
#define PLANE_EPS 1.f
#define MAX_HALF_WIDTH 2000.f
#define MIN_UP (-100.f)
#define MAX_UP 4000.f
int sm64_trees_count(uint32_t now) { return (now == sGen || now == sGen + 1u) ? sCount : 0; }
int sm64_trees_match(uint32_t now, const fc_vertex tri[3]) {
    if (now != sGen && now != sGen + 1u) { return -1; }
    int best = -1;
    float best_x = 0.f;
    for (int i = 0; i < sCount; i++) {
        float worst_x = 0.f;
        int ok = 1;
        for (int v = 0; v < 3 && ok; v++) {
            float l[3];
            local_of(&sTrees[i], tri[v].pos, l);
            ok = fabsf(l[2]) <= PLANE_EPS && fabsf(l[0]) <= MAX_HALF_WIDTH && l[1] >= MIN_UP && l[1] <= MAX_UP;
            if (fabsf(l[0]) > worst_x) { worst_x = fabsf(l[0]); }
        }
        if (ok && (best < 0 || worst_x < best_x)) { best = i; best_x = worst_x; }
    }
    return best;
}

static fc_vertex placed(const fc_vertex *src, const float p[3], const float r[3], const float b[3], float x, float y, float z) {
    fc_vertex o = *src;
    for (int k = 0; k < 3; k++) { o.pos[k] = p[k] + x * r[k] + y * (k == 1 ? 1.f : 0.f) + z * b[k]; }
    return o;
}
uint32_t sm64_trees_extra(int index, const fc_vertex tri[3], fc_vertex out[SM64_TREES_EXTRA_MAX]) {
    if (index < 0 || index >= sCount) { return 0; }
    const tree *t = &sTrees[index];
    float l[3][3];
    for (int v = 0; v < 3; v++) { local_of(t, tri[v].pos, l[v]); }
    uint32_t n = 0;
    const double yaw = atan2((double)t->b[0], (double)t->b[2]);
    for (int c = 1; c <= 2; c++) {
        float r[3], u[3], b[3];
        axes_at(yaw + c * STEP_RAD, r, u, b);
        for (int v = 0; v < 3; v++) { out[n++] = placed(&tri[v], t->p, r, b, l[v][0], l[v][1], l[v][2]); }
    }
    return n;
}
