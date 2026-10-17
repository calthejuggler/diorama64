#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef struct sm64_vis_tri { float a[3], b[3], c[3], n[3]; } sm64_vis_tri;
int sm64_vis_blocked(const sm64_vis_tri *tris, int n, const float eye[3], const float p[3]);
#ifdef __cplusplus
}
#endif
