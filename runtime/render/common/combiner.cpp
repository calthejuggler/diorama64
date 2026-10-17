#include "combiner.h"
#include <initializer_list>
namespace ar {
// Kept options. FOG: world fog is off and SCREEN fog is not emulated. WORLD_GEOMETRY:
// coopdx colour-grading flags, not part of the N64 look.
static const uint32_t kKeptOpts = FC_CC_ALPHA | FC_CC_TEXTURE_EDGE | FC_CC_NOISE | FC_CC_2CYCLE | FC_CC_LIGHTMAP;

// Cycle 1's colour reads cycle 0's alpha through COMBINEDA: on the N64 the alpha combiner always runs, so keep cycle
// 0's alpha slots even when alpha blending is off (gfx_pc's GL generator cannot even compile that case).
static bool cycle1_rgb_reads_combined_alpha(uint64_t slots) {
    for (int o = 0; o < 4; o++) if (combiner_slot(slots, 1, 0, o) == FC_SRC_COMBINEDA) return true;
    return false;
}

fc_combiner_key normalize_combiner(fc_combiner_key k) {
    fc_combiner_key n{k.slots, k.opts & kKeptOpts, 0};
    if (n.opts & FC_CC_TEXTURE_EDGE) n.opts |= FC_CC_ALPHA;
    if (!(n.opts & FC_CC_2CYCLE)) n.slots &= 0x00000000FFFFFFFFull;
    if (!(n.opts & FC_CC_ALPHA)) {
        uint64_t drop = (0xFFFFull << 16) | (0xFFFFull << 48);
        if ((n.opts & FC_CC_2CYCLE) && cycle1_rgb_reads_combined_alpha(n.slots)) drop &= ~(0xFFFFull << 16);
        n.slots &= ~drop;
        n.opts &= ~(uint32_t)FC_CC_NOISE;                                                  // dither only touches alpha
    }
    return n;
}

CombinerIR decode_combiner(fc_combiner_key key) {
    fc_combiner_key k = normalize_combiner(key);
    CombinerIR ir;
    ir.two_cycle = (k.opts & FC_CC_2CYCLE) != 0;
    ir.alpha_enabled = (k.opts & FC_CC_ALPHA) != 0;
    ir.texture_edge = (k.opts & FC_CC_TEXTURE_EDGE) != 0;
    ir.dither = (k.opts & FC_CC_NOISE) != 0;
    ir.unsupported = (k.opts & FC_CC_LIGHTMAP) != 0;
    for (int c = 0; c < 2; c++)
        for (int o = 0; o < 4; o++) { ir.rgb[c][o] = combiner_slot(k.slots, c, 0, o); ir.alpha[c][o] = combiner_slot(k.slots, c, 1, o); }
    for (int c = 0; c < (ir.two_cycle ? 2 : 1); c++) {
        for (int o = 0; o < 4; o++) {
            for (uint8_t s : {ir.rgb[c][o], ir.alpha[c][o]}) {
                if (s == FC_SRC_TEXEL0 || s == FC_SRC_TEXEL0A) ir.uses_tex0 = true;
                if (s == FC_SRC_TEXEL1 || s == FC_SRC_TEXEL1A) ir.uses_tex1 = true;
                if (s == FC_SRC_NOISE) ir.uses_noise = true;
            }
        }
    }
    if (ir.dither) ir.uses_noise = true;
    return ir;
}
}
