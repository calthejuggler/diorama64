#pragma once
#include <stdint.h>
#include "fc_frame.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { SM64_TREES_CARD = 0, SM64_TREES_CROSSED = 1 };
void sm64_trees_set_style(int style);
int sm64_trees_style(void);
const char *sm64_trees_style_name(int style);
int sm64_trees_style_parse(const char *name);
void sm64_trees_fixed_axes(const float pivot[3], float right[3], float up[3], float back[3]);
void sm64_trees_add(uint32_t gen, const float pivot[3], const float r[3], const float u[3], const float b[3]);
/* Only the trees of the geo pass with gen now or now - 1 count: SM64 advances its counter once between the geo pass and
 * the display list (display_and_vsync). */
int sm64_trees_match(uint32_t now, const fc_vertex tri[3]);
int sm64_trees_count(uint32_t now);
#define SM64_TREES_EXTRA_MAX 6
uint32_t sm64_trees_extra(int tree, const fc_vertex tri[3], fc_vertex out[SM64_TREES_EXTRA_MAX]);
#ifdef __cplusplus
}
#endif
