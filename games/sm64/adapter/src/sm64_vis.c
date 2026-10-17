#include "sm64_vis.h"
#include <math.h>
static void sub(const float *a, const float *b, double *o) { for (int k = 0; k < 3; k++) o[k] = (double)a[k] - (double)b[k]; }
static void cross(const double *a, const double *b, double *o) {
    o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0];
}
static double dot(const double *a, const double *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
int sm64_vis_blocked(const sm64_vis_tri *t, int n, const float eye[3], const float p[3]) {
    double d[3];
    sub(p, eye, d);
    if (dot(d, d) < 1e-6) return 0;
    for (int i = 0; i < n; i++) {
        const double nn[3] = { t[i].n[0], t[i].n[1], t[i].n[2] };
        if (!(dot(nn, d) < 0.0)) continue;
        double e1[3], e2[3], s[3], h[3], q[3];
        sub(t[i].b, t[i].a, e1); sub(t[i].c, t[i].a, e2);
        cross(d, e2, h);
        const double det = dot(e1, h);
        if (fabs(det) < 1e-12) continue;
        sub(eye, t[i].a, s);
        const double u = dot(s, h) / det;
        if (u < 0.0 || u > 1.0) continue;
        cross(s, e1, q);
        const double v = dot(d, q) / det;
        if (v < 0.0 || u + v > 1.0) continue;
        const double f = dot(e2, q) / det;
        if (f > 0.001 && f < 0.999) return 1;
    }
    return 0;
}
