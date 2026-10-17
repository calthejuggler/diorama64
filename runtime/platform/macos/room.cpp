#include "room.h"
#include "scene_shapes.h"
namespace ar {
void build_room(fc_builder *b) {
    const float fy = -0.75f, wy = 2.25f, e = 4.f;
    const uint8_t floor_c[4] = {120, 112, 100, 255}, wall_a[4] = {196, 200, 206, 255}, wall_b[4] = {176, 182, 190, 255};
    const uint8_t top[4] = {150, 105, 70, 255}, side[4] = {110, 76, 50, 255};
    const float floor_q[4][3] = {{-e, fy, -e}, {-e, fy, e}, {e, fy, e}, {e, fy, -e}};
    const float back[4][3] = {{-e, wy, -e}, {-e, fy, -e}, {e, fy, -e}, {e, wy, -e}};
    const float front[4][3] = {{e, wy, e}, {e, fy, e}, {-e, fy, e}, {-e, wy, e}};
    const float left[4][3] = {{-e, wy, e}, {-e, fy, e}, {-e, fy, -e}, {-e, wy, -e}};
    const float right[4][3] = {{e, wy, -e}, {e, fy, -e}, {e, fy, e}, {e, wy, e}};
    fc_add_quad(b, floor_q, floor_c); fc_add_quad(b, back, wall_a); fc_add_quad(b, front, wall_a); fc_add_quad(b, left, wall_b); fc_add_quad(b, right, wall_b);
    fc_add_box(b, -0.85f, -0.75f, -0.85f, 0.85f, -0.004f, 0.85f, top, side);
}
}
