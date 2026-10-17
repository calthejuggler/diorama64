#include "draw_list.h"
#include "combiner.h"
#include <cstring>
namespace ar {
void build_packets(const fc_draw *draws, uint32_t n, std::vector<DrawPacket> &out) {
    out.clear();
    out.reserve(n);
    for (uint32_t i = 0; i < n; i++) {
        const fc_draw &d = draws[i];
        DrawPacket p{};
        p.first_vertex = d.first_vertex; p.vertex_count = d.vertex_count;
        p.tex[0] = d.tex[0]; p.tex[1] = d.tex[1]; p.samp[0] = d.samp[0]; p.samp[1] = d.samp[1];
        p.cc = normalize_combiner(d.cc);
        for (int k = 0; k < 4; k++) { p.prim[k] = d.prim[k] / 255.f; p.env[k] = d.env[k] / 255.f; }
        p.lod_frac = d.lod_frac;
        p.depth_test = (d.render_flags & FC_RS_DEPTH_TEST) != 0;
        p.depth_write = (d.render_flags & FC_RS_DEPTH_WRITE) != 0;
        p.decal = (d.render_flags & FC_RS_DECAL) != 0;
        p.blend = (d.render_flags & FC_RS_BLEND) != 0;
        p.cull = d.cull;
        p.focus = d.focus;
        p.light_flags = d.light_flags;
        p.light_role = d.light_role;
        p.fade = 1.f;
        if (d.fade != 0 && d.space == FC_SPACE_WORLD) {   // a WORLD draw fading in: blended at its share, its depth kept
            p.fade = d.fade / 255.f;                       // (what lies behind it never shows through at full strength)
            p.faded = !p.blend;
            p.blend = 1;
        }
        p.clip_on = (d.render_flags & FC_RS_CLIP) != 0;
        std::memcpy(p.clip, d.clip, sizeof p.clip);
        out.push_back(p);
    }
}
}
