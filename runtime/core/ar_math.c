#include "ar_math.h"
#include <math.h>
#include <string.h>

ar_mat4 ar_mat4_identity(void) {
    ar_mat4 r; memset(&r, 0, sizeof r);
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.f;
    return r;
}

ar_mat4 ar_mat4_mul(ar_mat4 a, ar_mat4 b) {
    ar_mat4 r;
    for (int c = 0; c < 4; c++) {
        for (int row = 0; row < 4; row++) {
            float s = 0.f;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    }
    return r;
}

ar_mat4 ar_mat4_from_pose(ar_pose p) {
    float x = p.quat[0], y = p.quat[1], z = p.quat[2], w = p.quat[3];
    ar_mat4 r = ar_mat4_identity();
    r.m[0] = 1.f - 2.f * (y * y + z * z); r.m[1] = 2.f * (x * y + w * z);       r.m[2] = 2.f * (x * z - w * y);
    r.m[4] = 2.f * (x * y - w * z);       r.m[5] = 1.f - 2.f * (x * x + z * z); r.m[6] = 2.f * (y * z + w * x);
    r.m[8] = 2.f * (x * z + w * y);       r.m[9] = 2.f * (y * z - w * x);       r.m[10] = 1.f - 2.f * (x * x + y * y);
    r.m[12] = p.pos[0]; r.m[13] = p.pos[1]; r.m[14] = p.pos[2];
    return r;
}

ar_mat4 ar_mat4_inverse_rigid(ar_mat4 m) {
    ar_mat4 r = ar_mat4_identity();
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r.m[j * 4 + i] = m.m[i * 4 + j];
    for (int i = 0; i < 3; i++)
        r.m[12 + i] = -(r.m[0 * 4 + i] * m.m[12] + r.m[1 * 4 + i] * m.m[13] + r.m[2 * 4 + i] * m.m[14]);
    return r;
}

void ar_mat4_apply(ar_mat4 m, const float in[3], float out[4]) {
    for (int row = 0; row < 4; row++)
        out[row] = m.m[row] * in[0] + m.m[4 + row] * in[1] + m.m[8 + row] * in[2] + m.m[12 + row];
}

ar_mat4 ar_proj_reverse_z(ar_fov f, float n) {
    ar_mat4 r; memset(&r, 0, sizeof r);
    r.m[0] = 2.f / (f.right - f.left);
    r.m[5] = 2.f / (f.up - f.down);
    r.m[8] = (f.right + f.left) / (f.right - f.left);
    r.m[9] = (f.up + f.down) / (f.up - f.down);
    r.m[11] = -1.f;
    r.m[14] = n;
    return r;
}

ar_mat4 ar_table_from_level(float upm, float ground_y) {
    float s = 1.f / upm;
    ar_mat4 r = ar_mat4_identity();
    r.m[0] = r.m[5] = r.m[10] = s;
    r.m[13] = -ground_y * s;
    return r;
}

void ar_level_from_table_point(float upm, float ground_y, const float in[3], float out[3]) {
    out[0] = in[0] * upm;
    out[1] = in[1] * upm + ground_y;
    out[2] = in[2] * upm;
}

ar_pose ar_pose_identity(void) { ar_pose p = {{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 1.f}}; return p; }

static void qmul(const float a[4], const float b[4], float o[4]) {
    float r[4];
    r[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    r[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    r[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    r[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    memcpy(o, r, sizeof r);
}

void ar_quat_rotate(const float q[4], const float v[3], float out[3]) {
    float t[3] = { 2.f * (q[1] * v[2] - q[2] * v[1]), 2.f * (q[2] * v[0] - q[0] * v[2]), 2.f * (q[0] * v[1] - q[1] * v[0]) };
    float r[3] = { v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]),
                   v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]),
                   v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]) };
    memcpy(out, r, sizeof r);
}

ar_pose ar_pose_mul(ar_pose a, ar_pose b) {
    ar_pose r; float t[3];
    ar_quat_rotate(a.quat, b.pos, t);
    for (int k = 0; k < 3; k++) r.pos[k] = a.pos[k] + t[k];
    qmul(a.quat, b.quat, r.quat);
    return r;
}

ar_pose ar_pose_inverse(ar_pose p) {
    ar_pose r;
    r.quat[0] = -p.quat[0]; r.quat[1] = -p.quat[1]; r.quat[2] = -p.quat[2]; r.quat[3] = p.quat[3];
    float t[3]; ar_quat_rotate(r.quat, p.pos, t);
    for (int k = 0; k < 3; k++) r.pos[k] = -t[k];
    return r;
}

static void normalize3(float v[3]) {
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 0.f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

void ar_basis_from_forward(const float fwd[3], float right[3], float up[3]) {
    float f[3] = { fwd[0], fwd[1], fwd[2] };
    float len = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (!(len > 1e-6f)) { f[0] = 0.f; f[1] = 0.f; f[2] = -1.f; } else { f[0] /= len; f[1] /= len; f[2] /= len; }
    float wu[3] = { 0.f, 1.f, 0.f };
    if (fabsf(f[1]) > 0.999f) { wu[1] = 0.f; wu[2] = f[1] < 0.f ? -1.f : 1.f; }
    right[0] = f[1] * wu[2] - f[2] * wu[1];
    right[1] = f[2] * wu[0] - f[0] * wu[2];
    right[2] = f[0] * wu[1] - f[1] * wu[0];
    normalize3(right);
    up[0] = right[1] * f[2] - right[2] * f[1];
    up[1] = right[2] * f[0] - right[0] * f[2];
    up[2] = right[0] * f[1] - right[1] * f[0];
}

ar_pose ar_pose_look_at(const float eye[3], const float target[3]) {
    float f[3] = { target[0] - eye[0], target[1] - eye[1], target[2] - eye[2] };
    float r[3], u[3];
    ar_basis_from_forward(f, r, u);
    float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    float b[3];
    if (fl > 1e-6f) { b[0] = -f[0] / fl; b[1] = -f[1] / fl; b[2] = -f[2] / fl; } else { b[0] = 0.f; b[1] = 0.f; b[2] = 1.f; }
    if (fabsf(b[1]) > 0.999f || !(fl > 1e-6f)) {
        b[0] = r[1] * u[2] - r[2] * u[1]; b[1] = r[2] * u[0] - r[0] * u[2]; b[2] = r[0] * u[1] - r[1] * u[0];
    }
    float m00 = r[0], m10 = r[1], m20 = r[2], m01 = u[0], m11 = u[1], m21 = u[2], m02 = b[0], m12 = b[1], m22 = b[2];
    ar_pose p; memcpy(p.pos, eye, sizeof p.pos);
    float tr = m00 + m11 + m22;
    if (tr > 0.f) {
        float s = sqrtf(tr + 1.f) * 2.f;
        p.quat[3] = 0.25f * s; p.quat[0] = (m21 - m12) / s; p.quat[1] = (m02 - m20) / s; p.quat[2] = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        float s = sqrtf(1.f + m00 - m11 - m22) * 2.f;
        p.quat[3] = (m21 - m12) / s; p.quat[0] = 0.25f * s; p.quat[1] = (m01 + m10) / s; p.quat[2] = (m02 + m20) / s;
    } else if (m11 > m22) {
        float s = sqrtf(1.f + m11 - m00 - m22) * 2.f;
        p.quat[3] = (m02 - m20) / s; p.quat[0] = (m01 + m10) / s; p.quat[1] = 0.25f * s; p.quat[2] = (m12 + m21) / s;
    } else {
        float s = sqrtf(1.f + m22 - m00 - m11) * 2.f;
        p.quat[3] = (m10 - m01) / s; p.quat[0] = (m02 + m20) / s; p.quat[1] = (m12 + m21) / s; p.quat[2] = 0.25f * s;
    }
    return p;
}

int ar_fov_from_projection(const float m[16], ar_fov *fov, float *near_m) {
    /* Relative: the compositor's per-frame matrices carry ~1e-6 rounding terms; a finite far plane (m10 = near / (far -
     * near), e.g. 1e-3 at 100 m) is still refused. */
    const float eps = 1e-4f * fmaxf(1.f, fmaxf(fabsf(m[0]), fabsf(m[5])));
    const int zero[] = {1, 2, 3, 4, 6, 7, 10, 12, 13, 15};
    for (unsigned i = 0; i < sizeof zero / sizeof zero[0]; i++) if (!(fabsf(m[zero[i]]) <= eps)) return 0;
    if (!(m[0] > 0.f) || !(m[5] > 0.f) || !(fabsf(m[11] + 1.f) <= eps) || !(m[14] > 0.f)) return 0;
    if (!isfinite(m[0]) || !isfinite(m[5]) || !isfinite(m[8]) || !isfinite(m[9]) || !isfinite(m[14])) return 0;
    fov->right = (m[8] + 1.f) / m[0];
    fov->left = (m[8] - 1.f) / m[0];
    fov->up = (m[9] + 1.f) / m[5];
    fov->down = (m[9] - 1.f) / m[5];
    *near_m = m[14];
    return 1;
}
ar_pose ar_pose_from_mat4(ar_mat4 a) {
    const float *m = a.m;   /* R[row][col] = m[col * 4 + row] */
    const float r00 = m[0], r10 = m[1], r20 = m[2], r01 = m[4], r11 = m[5], r21 = m[6], r02 = m[8], r12 = m[9], r22 = m[10];
    ar_pose p;
    p.pos[0] = m[12]; p.pos[1] = m[13]; p.pos[2] = m[14];
    float x, y, z, w;
    const float tr = r00 + r11 + r22;
    if (tr > 0.f) {
        const float s = sqrtf(tr + 1.f) * 2.f;
        w = 0.25f * s; x = (r21 - r12) / s; y = (r02 - r20) / s; z = (r10 - r01) / s;
    } else if (r00 > r11 && r00 > r22) {
        const float s = sqrtf(1.f + r00 - r11 - r22) * 2.f;
        w = (r21 - r12) / s; x = 0.25f * s; y = (r01 + r10) / s; z = (r02 + r20) / s;
    } else if (r11 > r22) {
        const float s = sqrtf(1.f + r11 - r00 - r22) * 2.f;
        w = (r02 - r20) / s; x = (r01 + r10) / s; y = 0.25f * s; z = (r12 + r21) / s;
    } else {
        const float s = sqrtf(1.f + r22 - r00 - r11) * 2.f;
        w = (r10 - r01) / s; x = (r02 + r20) / s; y = (r12 + r21) / s; z = 0.25f * s;
    }
    const float n = sqrtf(x * x + y * y + z * z + w * w), sgn = w < 0.f ? -1.f : 1.f;
    p.quat[0] = sgn * x / n; p.quat[1] = sgn * y / n; p.quat[2] = sgn * z / n; p.quat[3] = sgn * w / n;
    return p;
}
