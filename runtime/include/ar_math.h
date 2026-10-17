/* runtime/include/ar_math.h — coordinate chain: tracking -> table (metres) -> level (units). */
#pragma once
#include "ar_types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ar_mat4 { float m[16]; } ar_mat4;   /* column-major, v' = M * v (Metal/simd layout) */
ar_mat4 ar_mat4_identity(void);
ar_mat4 ar_mat4_mul(ar_mat4 a, ar_mat4 b);
ar_mat4 ar_mat4_from_pose(ar_pose p);
ar_mat4 ar_mat4_inverse_rigid(ar_mat4 m);
void    ar_mat4_apply(ar_mat4 m, const float in[3], float out[4]);
/* Infinite-far reverse-Z for Metal clip space (z in [0,1], depth 1 at near_m, -> 0 at infinity). */
ar_mat4 ar_proj_reverse_z(ar_fov fov, float near_m);
/* Inverse of ar_proj_reverse_z for a compositor matrix (column-major, right-up-back eye space, infinite far, reverse-Z:
 * m[10] == 0, m[11] == -1, m[14] == near, m[15] == 0). 1 with the tangents and near plane; 0 if m has another form. */
int     ar_fov_from_projection(const float m[16], ar_fov *fov, float *near_m);
ar_pose ar_pose_from_mat4(ar_mat4 m);
/* Level units -> table metres: uniform 1/units_per_meter scale; the level's ground_y lands on table y = 0.
 * Placement yaw is part of tracking_from_table, so level and table axes coincide. */
ar_mat4 ar_table_from_level(float units_per_meter, float ground_y);
void    ar_level_from_table_point(float units_per_meter, float ground_y, const float in[3], float out[3]);
ar_pose ar_pose_identity(void);
ar_pose ar_pose_mul(ar_pose a, ar_pose b);                    /* a ∘ b: first b, then a */
ar_pose ar_pose_inverse(ar_pose p);
void    ar_quat_rotate(const float q[4], const float v[3], float out[3]);
/* Orthonormal right/up for a viewer looking along fwd. Finite for any input: straight up/down picks
 * -Z (down) or +Z (up) as screen-up; a zero vector is treated as -Z. */
void    ar_basis_from_forward(const float fwd[3], float right[3], float up[3]);
ar_pose ar_pose_look_at(const float eye[3], const float target[3]);
#ifdef __cplusplus
}
#endif
