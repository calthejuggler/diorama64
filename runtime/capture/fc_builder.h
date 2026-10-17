/* The frame stays valid until the next reset. */
#pragma once
#include "fc_frame.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct fc_builder fc_builder;
fc_builder *fc_builder_create(void);
void fc_builder_destroy(fc_builder *b);
void fc_builder_reset(fc_builder *b, uint64_t seq, uint32_t screen_w, uint32_t screen_h);
fc_vertex *fc_builder_add_draw(fc_builder *b, const fc_draw *proto, uint32_t vertex_count);
void fc_builder_count(fc_builder *b, uint32_t discarded, uint32_t unsupported);
void fc_builder_set_group(fc_builder *b, const fc_overlay_group *g);
void fc_builder_add_polyline(fc_builder *b, const float *xyz, uint32_t n, const uint8_t rgba[4], float width_m);
/* Static draws (FC_LF_STATIC) and the others are two separate weld sets, so moving objects never bend the terrain's
 * normals. */
void fc_builder_derive_normals(fc_builder *b, uint8_t flag_mask, float crease_deg, float weld_units);
/* A nonzero static_key: the caller vouches that equal keys mean equal static positions, and the per-vertex hash is
 * skipped. */
void fc_builder_derive_normals_keyed(fc_builder *b, uint8_t flag_mask, float crease_deg, float weld_units, uint64_t static_key);
uint32_t fc_builder_normal_cache_hits(const fc_builder *b);
uint32_t fc_builder_normal_parallel_runs(const fc_builder *b);
/* Call after the last add_draw; fc_builder_reset clears it. */
float *fc_builder_prev_positions(fc_builder *b);
/* prev_pos gets the same d, so d must equal the previous tick's for the interpolation to be right. */
void fc_builder_translate_world(fc_builder *b, const float d[3]);
void fc_builder_set_level(fc_builder *b, const ar_level_info *level);
uint32_t fc_builder_prev_dropped(const fc_builder *b);
void fc_builder_set_game_view(fc_builder *b, const fc_game_view *v);
void fc_builder_set_focus_styles(fc_builder *b, const fc_focus_style *s, uint32_t n);
void fc_builder_set_look(fc_builder *b, const fc_look *l);
uint32_t fc_builder_vertex_count(const fc_builder *b);
const fc_frame *fc_builder_finish(fc_builder *b);
#ifdef __cplusplus
}
#endif
