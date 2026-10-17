#include "shading_view.h"
#include <string.h>
#include "ar_math.h"
void sm64_shading_view_from_quat(const float q[4], float m[4][4]) {
    const float ax[3][3] = { { 1.f, 0.f, 0.f }, { 0.f, 1.f, 0.f }, { 0.f, 0.f, 1.f } };
    float col[3][3];
    for (int c = 0; c < 3; c++) { ar_quat_rotate(q, ax[c], col[c]); }
    memset(m, 0, sizeof(float) * 16);
    for (int i = 0; i < 3; i++) { for (int c = 0; c < 3; c++) { m[i][c] = col[c][i]; } }
    m[3][3] = 1.f;
}
