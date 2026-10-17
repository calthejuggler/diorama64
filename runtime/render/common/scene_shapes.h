#pragma once
#include <cstdint>
extern "C" {
#include "fc_builder.h"
}
namespace ar {
void fc_add_quad(fc_builder *b, const float p[4][3], const uint8_t rgba[4]);
void fc_add_box(fc_builder *b, float x0, float y0, float z0, float x1, float y1, float z1, const uint8_t top[4], const uint8_t side[4]);
constexpr float kFakeTableHalfX = 0.6f, kFakeTableHalfZ = 0.4f;
void build_fake_table(fc_builder *b, float half_x, float half_z);
}
