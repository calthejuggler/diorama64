#include "scene_shapes.h"
#include <cstring>
namespace ar {
void fc_add_quad(fc_builder *b, const float p[4][3], const uint8_t c[4]) {
    fc_draw d{};
    d.space = FC_SPACE_WORLD;
    d.render_flags = FC_RS_DEPTH_TEST | FC_RS_DEPTH_WRITE;
    d.cc.slots = ((uint64_t)FC_SRC_SHADE << 12) | ((uint64_t)FC_SRC_SHADE << 28);
    fc_vertex *v = fc_builder_add_draw(b, &d, 6);
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; i++) { v[i] = fc_vertex{}; std::memcpy(v[i].pos, p[idx[i]], 12); std::memcpy(v[i].shade, c, 4); }
}
void fc_add_box(fc_builder *b, float x0, float y0, float z0, float x1, float y1, float z1, const uint8_t top[4], const uint8_t side[4]) {
    const float t[4][3] = {{x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}};
    const float f[4][3] = {{x0, y1, z1}, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}};
    const float k[4][3] = {{x1, y1, z0}, {x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}};
    const float l[4][3] = {{x0, y1, z0}, {x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}};
    const float r[4][3] = {{x1, y1, z1}, {x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}};
    fc_add_quad(b, t, top); fc_add_quad(b, f, side); fc_add_quad(b, k, side); fc_add_quad(b, l, side); fc_add_quad(b, r, side);
}
void build_fake_table(fc_builder *b, float hx, float hz) {
    const uint8_t top[4] = {150, 105, 70, 255}, side[4] = {110, 76, 50, 255};
    fc_add_box(b, -hx, -0.032f, -hz, hx, -0.002f, hz, top, side);
}
}
