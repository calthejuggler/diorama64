#pragma once
#include <cstdint>
#include "fc_frame.h"
namespace ar {
// Half: texels under half alpha write no depth. Faint: texels with any alpha write depth only where none was written
// yet. Prepass: a clear transition's prepass (its alpha test before the fade).
enum : uint8_t { kDepthOnlyNone = 0, kDepthOnlyHalf = 1, kDepthOnlyFaint = 2, kDepthOnlyPrepass = 3 };
struct CombinerIR {
    uint8_t rgb[2][4] = {};        // FC_SRC_* operands (a, b, c, d) per cycle: (a - b) * c + d
    uint8_t alpha[2][4] = {};
    bool two_cycle = false, alpha_enabled = false, texture_edge = false, alpha_coverage = false, dither = false, unsupported = false;
    bool uses_tex0 = false, uses_tex1 = false, uses_noise = false;
    bool flat_color = false;
    bool flat_cutout = false;
    uint8_t depth_fill = 0;
    bool table_shadow = false;
    bool clip = false;
    bool layer_out = true;         // vmain writes [[render_target_array_index]]; false: a plain flat varying, for
                                   // multisample pipelines on GPUs that cannot rasterize multisample layers
    bool cutaway = false;
};
inline uint8_t combiner_slot(uint64_t slots, int cycle, int channel, int operand) {
    return (uint8_t)((slots >> (4 * ((cycle * 2 + channel) * 4 + operand))) & 0xFu);
}
fc_combiner_key normalize_combiner(fc_combiner_key k);
CombinerIR decode_combiner(fc_combiner_key k);
}
