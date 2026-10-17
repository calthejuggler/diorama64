#import <Metal/Metal.h>
#include "vos_contract.h"
#include <cstdio>
namespace ar {
std::string vos_contract_string(cp_layer_renderer_t lr, cp_drawable_t d) {
    char b[512];
    std::string s;
    cp_layer_renderer_configuration_t cfg = cp_layer_renderer_get_configuration(lr);
    const simd_float2 dr = cp_drawable_get_depth_range(d);   // x = far, y = near (reverse-Z)
    std::snprintf(b, sizeof b, "layout=%u foveation=%d colorFmt=%lu depthFmt=%lu target=%d views=%zu textures=%zu ratemaps=%zu depthRange=[%g,%g]",
                  (unsigned)cp_layer_renderer_configuration_get_layout(cfg), (int)cp_layer_renderer_configuration_get_foveation_enabled(cfg),
                  (unsigned long)cp_layer_renderer_configuration_get_color_format(cfg),
                  (unsigned long)cp_layer_renderer_configuration_get_depth_format(cfg), (int)cp_drawable_get_target(d),
                  cp_drawable_get_view_count(d), cp_drawable_get_texture_count(d), cp_drawable_get_rasterization_rate_map_count(d),
                  (double)dr.x, (double)dr.y);
    s += b;
    for (size_t t = 0; t < cp_drawable_get_texture_count(d); t++) {
        id<MTLTexture> c = cp_drawable_get_color_texture(d, t), z = cp_drawable_get_depth_texture(d, t);
        std::snprintf(b, sizeof b, " tex%zu={color %lux%lu x%lu fmt=%lu type=%lu, depth fmt=%lu}", t, (unsigned long)c.width,
                      (unsigned long)c.height, (unsigned long)c.arrayLength, (unsigned long)c.pixelFormat,
                      (unsigned long)c.textureType, (unsigned long)(z ? z.pixelFormat : 0));
        s += b;
    }
    for (size_t v = 0; v < cp_drawable_get_view_count(d); v++) {
        cp_view_t view = cp_drawable_get_view(d, v);
        cp_view_texture_map_t tm = cp_view_get_view_texture_map(view);
        const MTLViewport vp = cp_view_texture_map_get_viewport(tm);
        const simd_float4x4 eye = cp_view_get_transform(view);
        const simd_float4x4 p = cp_drawable_compute_projection(d, cp_axis_direction_convention_right_up_back, v);
        std::snprintf(b, sizeof b, " view%zu={tex=%zu slice=%zu vp=%g,%g,%gx%g eye=%g,%g,%g P00=%g P11=%g P20=%g P21=%g P22=%g P23=%g P32=%g P33=%g}",
                      v, cp_view_texture_map_get_texture_index(tm), cp_view_texture_map_get_slice_index(tm), vp.originX, vp.originY,
                      vp.width, vp.height, (double)eye.columns[3].x, (double)eye.columns[3].y, (double)eye.columns[3].z,
                      (double)p.columns[0].x, (double)p.columns[1].y, (double)p.columns[2].x, (double)p.columns[2].y,
                      (double)p.columns[2].z, (double)p.columns[2].w, (double)p.columns[3].z, (double)p.columns[3].w);
        s += b;
    }
    return s;
}
}
