#pragma once
#ifdef __cplusplus
extern "C" {
#endif
/* SM64 row-vector view rotation (v_eye = v_level * m); quat xyzw in level axes. No translation. */
void sm64_shading_view_from_quat(const float quat[4], float m[4][4]);
#ifdef __cplusplus
}
#endif
