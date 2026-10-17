#include "ground_height.h"
#include <math.h>
#include <stdlib.h>

#define GH_BASE_FRACTION 0.75

static float horiz_area(const gh_tri *t) {
    float ax = t->x[1] - t->x[0], az = t->z[1] - t->z[0];
    float bx = t->x[2] - t->x[0], bz = t->z[2] - t->z[0];
    return fabsf(ax * bz - az * bx) * 0.5f;
}

float gh_ground_height(const gh_tri *tris, int count, float bin_size) {
    if (count <= 0 || !tris || !(bin_size > 0.f)) { return 0.f; }
    float lo = INFINITY, hi = -INFINITY;
    for (int i = 0; i < count; i++) {
        float y = (tris[i].y[0] + tris[i].y[1] + tris[i].y[2]) / 3.f;
        if (y < lo) lo = y;
        if (y > hi) hi = y;
    }
    int nb = (int)((hi - lo) / bin_size) + 1;
    double *w = calloc((size_t)nb, sizeof *w);
    if (!w) { return 0.f; }
    for (int i = 0; i < count; i++) {
        float y = (tris[i].y[0] + tris[i].y[1] + tris[i].y[2]) / 3.f;
        w[(int)((y - lo) / bin_size)] += horiz_area(&tris[i]);
    }
    int best = 0;
    for (int b = 1; b < nb; b++) if (w[b] > w[best]) best = b;
    /* Several raised terraces can outweigh the true base plane by a few percent (BoB: y~768
     * terraces 36M vs the y=0 field 33M). The tabletop wants the base, so prefer the lowest
     * bin that is nearly as large as the winner. */
    for (int b = 0; b < best; b++) if (w[b] >= GH_BASE_FRACTION * w[best]) { best = b; break; }
    free(w);
    double sum = 0, wsum = 0;
    for (int i = 0; i < count; i++) {
        float y = (tris[i].y[0] + tris[i].y[1] + tris[i].y[2]) / 3.f;
        if ((int)((y - lo) / bin_size) == best) { double a = horiz_area(&tris[i]); sum += a * y; wsum += a; }
    }
    return wsum > 0 ? (float)(sum / wsum) : lo + (best + 0.5f) * bin_size;
}

float gh_table_ground(const gh_tri *tris, int count, float bin_size, float fallback) {
    if (count <= 0 || !tris) { return fallback; }
    gh_tri *kept = malloc(sizeof *kept * (size_t)count);
    if (!kept) { return fallback; }
    int n = 0;
    float lowest = INFINITY;
    for (int i = 0; i < count; i++) {
        const float *y = tris[i].y;
        if (y[0] <= GH_VOID_Y && y[1] <= GH_VOID_Y && y[2] <= GH_VOID_Y) continue;
        kept[n++] = tris[i];
        for (int k = 0; k < 3; k++) if (y[k] < lowest) lowest = y[k];
    }
    float r = fallback;
    if (n > 0) {
        const float plane = gh_ground_height(kept, n, bin_size);
        r = plane - lowest <= GH_DIP_TOLERANCE ? plane : lowest;
    }
    free(kept);
    return r;
}

float gh_lowest_floor(const gh_tri *tris, int count, float fallback) {
    float lowest = INFINITY;
    for (int i = 0; tris && i < count; i++) {
        const float *y = tris[i].y;
        if (y[0] <= GH_VOID_Y && y[1] <= GH_VOID_Y && y[2] <= GH_VOID_Y) continue;
        for (int k = 0; k < 3; k++) if (y[k] > GH_VOID_Y && y[k] < lowest) lowest = y[k];
    }
    return isfinite(lowest) ? lowest : fallback;
}

struct gh_key { float y; double w; };
static int cmp_key(const void *a, const void *b) {
    const float ya = ((const struct gh_key *)a)->y, yb = ((const struct gh_key *)b)->y;
    return ya < yb ? -1 : ya > yb ? 1 : 0;
}
float gh_floor_percentile(const gh_tri *tris, int count, double q, float fallback) {
    if (count <= 0 || !tris) { return fallback; }
    struct gh_key *k = malloc(sizeof *k * (size_t)count);
    if (!k) { return fallback; }
    int n = 0; double total = 0.0;
    for (int i = 0; i < count; i++) {
        const float *y = tris[i].y;
        if (y[0] <= GH_VOID_Y && y[1] <= GH_VOID_Y && y[2] <= GH_VOID_Y) continue;
        k[n].y = (y[0] + y[1] + y[2]) / 3.f; k[n].w = horiz_area(&tris[i]); total += k[n].w; n++;
    }
    float r = fallback;
    if (n > 0) {
        qsort(k, (size_t)n, sizeof *k, cmp_key);
        double cum = 0.0; int i = 0;
        for (; i < n - 1; i++) { cum += k[i].w; if (total > 0.0 && cum / total >= q) break; }
        r = k[i].y;
    }
    free(k);
    return r;
}

int gh_override_lookup(const gh_override *table, int n, int level, int area, float *out_y) {
    if (!table || !out_y) { return 0; }
    for (int i = 0; i < n; i++) {
        if (table[i].level == level && table[i].area == area) { *out_y = table[i].ground_y; return 1; }
    }
    return 0;
}

typedef void (*gh_floor_fn)(const int16_t *a, const int16_t *b, const int16_t *c, double nx, double ny, double nz, void *user);
static void terrain_each(const int16_t *terrain, int (*is_void)(int type), int (*has_force)(int type), int floors_only,
                         gh_floor_fn fn, void *user) {
    const int16_t *d = terrain, *verts = NULL;
    int nverts = 0;
    for (;;) {
        const int16_t cmd = *d++;
        if (cmd == 0x40) {                                   /* TERRAIN_LOAD_VERTICES */
            nverts = *d++; verts = d; d += 3 * nverts;
        } else if (cmd == 0x41) {                            /* TERRAIN_LOAD_CONTINUE */
            continue;
        } else if (cmd < 0x40 || cmd >= 0x65) {              /* a surface type and its triangles */
            const int n = *d++, stride = has_force && has_force(cmd) ? 4 : 3;
            const int skip = is_void && is_void(cmd);
            for (int i = 0; i < n; i++, d += stride) {
                if (skip || !verts || d[0] < 0 || d[1] < 0 || d[2] < 0 || d[0] >= nverts || d[1] >= nverts || d[2] >= nverts) continue;
                const int16_t *a = verts + 3 * d[0], *b = verts + 3 * d[1], *c = verts + 3 * d[2];
                /* the normal as read_surface_data computes it */
                const double nx = (double)(b[1] - a[1]) * (c[2] - b[2]) - (double)(b[2] - a[2]) * (c[1] - b[1]);
                const double ny = (double)(b[2] - a[2]) * (c[0] - b[0]) - (double)(b[0] - a[0]) * (c[2] - b[2]);
                const double nz = (double)(b[0] - a[0]) * (c[1] - b[1]) - (double)(b[1] - a[1]) * (c[0] - b[0]);
                const double len = sqrt(nx * nx + ny * ny + nz * nz);
                if (floors_only && (!(len > 0.0) || ny / len <= 0.01)) continue;
                fn(a, b, c, nx, ny, nz, user);
            }
        } else {                                             /* objects (0x43), environment (0x44), end (0x42) */
            break;
        }
    }
}
struct under { float x, y, z; int found; float best; };
static void floor_under(const int16_t *a, const int16_t *b, const int16_t *c, double nx, double ny, double nz, void *user) {
    struct under *u = user;
    const float x = u->x, z = u->z;
    /* inside in XZ, with find_floor's edge tests (counter-clockwise seen from above) */
    if ((a[2] - z) * (b[0] - a[0]) - (a[0] - x) * (b[2] - a[2]) < 0) return;
    if ((b[2] - z) * (c[0] - b[0]) - (b[0] - x) * (c[2] - b[2]) < 0) return;
    if ((c[2] - z) * (a[0] - c[0]) - (c[0] - x) * (a[2] - c[2]) < 0) return;
    const double h = -(nx * x + nz * z - (nx * a[0] + ny * a[1] + nz * a[2])) / ny;
    if (h > u->y + GH_FLOOR_REACH) return;
    if (!u->found || h > u->best) { u->best = (float)h; u->found = 1; }
}
int gh_terrain_floor_under(const int16_t *terrain, float x, float y, float z, int (*is_void)(int type),
                           int (*has_force)(int type), float *out_y) {
    if (!terrain || !out_y) { return 0; }
    struct under u = { x, y, z, 0, 0.f };
    terrain_each(terrain, is_void, has_force, 1, floor_under, &u);
    if (u.found) { *out_y = u.best; }
    return u.found;
}
struct collect { gh_tri *out; int cap, n; };
static void floor_collect(const int16_t *a, const int16_t *b, const int16_t *c, double nx, double ny, double nz, void *user) {
    (void)nx; (void)ny; (void)nz;
    struct collect *k = user;
    if (k->out && k->n < k->cap) {
        const gh_tri t = {{a[1], b[1], c[1]}, {a[0], b[0], c[0]}, {a[2], b[2], c[2]}};
        k->out[k->n] = t;
    }
    k->n++;
}
int gh_terrain_floors(const int16_t *terrain, int (*is_void)(int type), int (*has_force)(int type), gh_tri *out, int cap) {
    if (!terrain) { return 0; }
    struct collect k = { out, cap, 0 };
    terrain_each(terrain, is_void, has_force, 1, floor_collect, &k);
    return k.n;
}
float gh_entry_height(float entry_y, float floor_y) {
    return floor_y <= entry_y && entry_y - floor_y <= GH_FLOOR_REACH ? entry_y : floor_y;
}
float gh_snap_to_plane(float floor_y, const gh_tri *tris, int count, float bin_size) {
    if (count <= 0 || !tris) { return floor_y; }
    const float plane = gh_ground_height(tris, count, bin_size);
    return fabsf(plane - floor_y) <= GH_SAME_PLANE ? plane : floor_y;
}
struct bottom { int found; float y; };
static void tri_bottom(const int16_t *a, const int16_t *b, const int16_t *c, double nx, double ny, double nz, void *user) {
    (void)nx; (void)ny; (void)nz;
    struct bottom *u = user;
    const int16_t *v[3] = { a, b, c };
    for (int k = 0; k < 3; k++) {
        if (v[k][1] > GH_VOID_Y && (!u->found || v[k][1] < u->y)) { u->y = v[k][1]; u->found = 1; }
    }
}
int gh_terrain_bottom(const int16_t *terrain, int (*is_void)(int type), int (*has_force)(int type), float *out_y) {
    if (!terrain || !out_y) { return 0; }
    struct bottom u = { 0, 0.f };
    terrain_each(terrain, is_void, has_force, 0, tri_bottom, &u);
    if (u.found) { *out_y = u.y; }
    return u.found;
}
