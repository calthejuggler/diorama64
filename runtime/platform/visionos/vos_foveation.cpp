#include "vos_foveation.h"
namespace ar {
FoveationPlan vos_foveation_plan(const RateMapShape &m, bool layered, uint32_t view_count) {
    FoveationPlan p;
    if (m.maps == 0) return p;
    // The dedicated layout has a map per view, but its views share one side-by-side target here (one map per encoder).
    if (!layered) { p.reason = "a rate map in the dedicated layout"; return p; }
    if (m.maps != 1) { p.reason = "more than one rate map for a layered drawable"; return p; }
    if (view_count == 0 || m.layers < view_count || !m.physical) { p.reason = "the rate map has fewer layers than views"; return p; }
    if (m.screen[0] == 0 || m.screen[1] == 0) { p.reason = "the rate map has an empty screen size"; return p; }
    for (uint32_t i = 0; i < view_count; i++)
        if (m.physical[i][0] == 0 || m.physical[i][1] == 0 || m.physical[i][0] > m.texture[0] || m.physical[i][1] > m.texture[1]) {
            p.reason = "the rate map's physical size does not fit the drawable texture";
            return p;
        }
    p.use = true;
    p.target_w = m.texture[0]; p.target_h = m.texture[1]; p.target_layers = view_count;
    p.view_w = m.screen[0]; p.view_h = m.screen[1];
    return p;
}
}
