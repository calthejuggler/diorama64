#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { float y[3]; float x[3]; float z[3]; } gh_tri;
float gh_ground_height(const gh_tri *tris, int count, float bin_size);

#define GH_VOID_Y (-8000.f)
#define GH_DIP_TOLERANCE 400.f
float gh_table_ground(const gh_tri *tris, int count, float bin_size, float fallback);
float gh_lowest_floor(const gh_tri *tris, int count, float fallback);

#define GH_FLOOR_REACH 78.f
int gh_terrain_floor_under(const int16_t *terrain, float x, float y, float z, int (*is_void)(int type),
                           int (*has_force)(int type), float *out_y);

/* The entry y when it stands at most GH_FLOOR_REACH above the floor under it (a ramp under the point lies a little
 * lower: the castle hall's MARIO_POS y 0 over a step at -37), else the floor (Mario falls or swims down to it: WDW,
 * DDD). */
float gh_entry_height(float entry_y, float floor_y);
int gh_terrain_floors(const int16_t *terrain, int (*is_void)(int type), int (*has_force)(int type), gh_tri *out, int cap);
/* Within GH_SAME_PLANE the floor-mode plane wins: it keeps BoB's ground bit-identical (0.00427943). */
#define GH_SAME_PLANE 1.f
float gh_snap_to_plane(float floor_y, const gh_tri *tris, int count, float bin_size);

#define GH_BASE_PERCENTILE 0.05
float gh_floor_percentile(const gh_tri *tris, int count, double q, float fallback);
int gh_terrain_bottom(const int16_t *terrain, int (*is_void)(int type), int (*has_force)(int type), float *out_y);

typedef struct { int level; int area; float ground_y; } gh_override;
int gh_override_lookup(const gh_override *table, int n, int level, int area, float *out_y);
#ifdef __cplusplus
}
#endif
