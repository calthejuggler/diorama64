#include "renderer.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include "combiner.h"
#include "draw_list.h"
#include "shadow_fit.h"
namespace ar {
const SilhouetteStyle kSilhouetteGray = {{0.22f, 0.22f, 0.24f, 0.6f}};
const float kPanelBacking[4] = {0.05f, 0.05f, 0.08f, 0.55f};
const float kPanelBackingWhite[4] = {1.f, 1.f, 1.f, 1.f};
constexpr uint32_t kAoViews = 32, kAoMapSize = 512, kAoMaxDim = 128;
// AO is local: a surface occludes only within this distance (level units; 800 = 10 cm at 1:8000), so corners, undersides
// and wall bases darken while a room under a high ceiling (seen from above through it) does not go uniformly dark.
constexpr float kAoRadius = 800.f;
static DepthExtras depth_extras(const LightingFrame &l, const float *prev, float alpha) {
    DepthExtras x; x.prev_pos = prev; x.interp_alpha = alpha;
    std::memcpy(x.wclip_lo, l.wclip_lo, sizeof x.wclip_lo);
    std::memcpy(x.wclip_hi, l.wclip_hi, sizeof x.wclip_hi);
    return x;
}
// Shared with fixed_pipeline_draws, so the warm-up builds exactly what they draw.
static fc_combiner_key shade_combiner(bool alpha) {
    return normalize_combiner(fc_combiner_key{((uint64_t)FC_SRC_SHADE << 12) | ((uint64_t)FC_SRC_SHADE << 28), alpha ? (uint32_t)FC_CC_ALPHA : 0u, 0});
}
static DrawPacket table_shadow_packet() {
    DrawPacket p{};
    p.vertex_count = 6; p.table_shadow = 1;
    p.cc = shade_combiner(true);
    p.depth_test = 1; p.depth_write = 1; p.blend = 1; p.cull = FC_CULL_NONE;
    return p;
}
static DrawPacket backing_packet() {
    DrawPacket p{};
    p.vertex_count = 6;
    p.cc = shade_combiner(true);
    p.depth_test = 1; p.depth_write = 0; p.blend = 1; p.cull = FC_CULL_NONE;
    return p;
}
static DrawPacket overlay_depth_packet() {
    DrawPacket p{};
    p.first_vertex = 0; p.vertex_count = 6;
    p.cc = shade_combiner(false);
    p.depth_test = 1; p.depth_write = 1; p.cull = FC_CULL_NONE; p.depth_only = kDepthOnlyHalf;
    return p;
}
static DrawPacket game_view_packet() {
    DrawPacket p{};
    p.vertex_count = 6;
    p.cc = normalize_combiner(fc_combiner_key{((uint64_t)FC_SRC_TEXEL0 << 12) | ((uint64_t)FC_SRC_1 << 28), (uint32_t)FC_CC_ALPHA, 0});
    p.tex[0] = Renderer::kGameViewTexture;
    p.samp[0] = fc_sampler{1, FC_WRAP_CLAMP, FC_WRAP_CLAMP, 0};
    p.depth_test = 1; p.depth_write = 0; p.blend = 1; p.cull = FC_CULL_NONE;
    return p;
}
void Renderer::set_panel_backing(const float *rgba) {
    backing_on_ = rgba != nullptr;
    if (rgba) std::memcpy(backing_, rgba, sizeof backing_);
}
bool cutaway_reaches(const float eye[3], const float target[3], float cut_radius, const float c[3], float rad) {
    float a[3], v[3];
    for (int k = 0; k < 3; k++) { a[k] = target[k] - eye[k]; v[k] = c[k] - eye[k]; }
    const float len = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (!(len > 1e-6f) || !std::isfinite(len)) return false;
    const float t = (v[0] * a[0] + v[1] * a[1] + v[2] * a[2]) / len;
    if (t + rad <= 0.f || t - rad >= len) return false;
    float d2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2] - t * t;
    const float d = std::sqrt(std::max(d2, 0.f));
    return d < cut_radius * std::min(1.f, std::max(0.f, (t + rad) / len)) + rad;
}
static bool cut_on(const fc_frame &f, const LightingFrame *light) {
    return f.look && light && light->on && light->cut && light->cut_radius > 0.f;
}
// Static terrain (not an authored shadow) the cut may take: translucent terrain is never cut; decals are, and so is
// opaque terrain fading in: the cut grows with its fade (the shader's cut_away), so its wall's hole never pops in when
// the fade ends.
static bool cut_candidate(const DrawPacket &p) {
    if (!(p.light_flags & FC_LF_STATIC) || (p.light_flags & FC_LF_FAKE_SHADOW)) return false;
    return p.decal || !((p.blend && !p.faded) || !p.depth_write);
}
static bool warm_on(const fc_frame &f, const LightingFrame *light) {
    return f.look && light && light->on && (light->cut_warm || (light->cut && light->cut_radius > 0.f));
}
std::vector<DrawPacket> cutaway_warm_draws(const fc_frame &f, const LightingFrame *light, const std::vector<DrawPacket> &packets) {
    std::vector<DrawPacket> out;
    if (!warm_on(f, light)) return out;
    for (const DrawPacket &p : packets) {
        if (!cut_candidate(p)) continue;
        DrawPacket w = p;
        w.cutaway = 1;
        const PipeIdentity k = pipe_identity(w);
        if (std::any_of(out.begin(), out.end(), [&](const DrawPacket &q) { return pipe_identity(q) == k; })) continue;
        out.push_back(w);
    }
    return out;
}
void mark_cutaway(const fc_frame &f, const LightingFrame *light, const float eye[3], std::vector<DrawPacket> &packets) {
    const bool on = cut_on(f, light) && eye;
    for (DrawPacket &p : packets) {
        p.cutaway = 0;
        if (!on || !cut_candidate(p)) continue;
        if (p.vertex_count == 0 || !f.verts || (uint64_t)p.first_vertex + p.vertex_count > f.vert_count) continue;
        float lo[3] = {INFINITY, INFINITY, INFINITY}, hi[3] = {-INFINITY, -INFINITY, -INFINITY};
        for (uint32_t i = 0; i < p.vertex_count; i++) {
            const float *q = f.verts[p.first_vertex + i].pos;
            for (int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], q[k]); hi[k] = std::max(hi[k], q[k]); }
        }
        float c[3], r2 = 0.f;
        for (int k = 0; k < 3; k++) { c[k] = 0.5f * (lo[k] + hi[k]); r2 += 0.25f * (hi[k] - lo[k]) * (hi[k] - lo[k]); }
        p.cutaway = cutaway_reaches(eye, light->cut_target, light->cut_radius, c, std::sqrt(r2)) ? 1 : 0;
    }
}
void apply_clear_fade(std::vector<DrawPacket> &packets, float clear) {
    if (!(clear > 0.f)) return;
    if (clear >= 1.f) { packets.clear(); return; }
    const float keep = 1.f - clear;
    std::vector<DrawPacket> out;
    out.reserve(packets.size() * 2);
    for (const DrawPacket &p : packets) {
        if (!p.depth_write || !p.depth_test || p.depth_only || p.silhouette) continue;
        DrawPacket z = p;
        z.depth_only = kDepthOnlyPrepass; z.blend = 0; z.faded = 0; z.focus = 0;
        out.push_back(z);
    }
    for (DrawPacket p : packets) {
        p.trans_alpha *= keep;   // not fade, which the cutaway grows with: the copies and the colour cut alike
        if (!p.blend && !p.depth_only) { p.faded = 1; p.blend = 1; }
        out.push_back(p);
    }
    packets.swap(out);
}
size_t ClearWarmKeyHash::operator()(const ClearWarmKey &k) const {
    uint64_t h = k.id.slots * 0x9E3779B97F4A7C15ull;
    const uint8_t b[8] = {k.id.blend, k.id.silhouette, k.id.depth_only, k.id.table_shadow, k.id.clip, k.id.cut, k.writes, 0};
    for (uint64_t v : {(uint64_t)k.id.opts, (uint64_t)k.samples}) h = (h ^ v) * 0x100000001B3ull;
    for (uint8_t v : b) h = (h ^ v) * 0x100000001B3ull;
    return (size_t)h;
}
std::vector<DrawPacket> clear_fade_warm_draws(const LightingFrame *light, const std::vector<DrawPacket> &packets,
                                              ClearWarmSet *seen, uint32_t samples) {
    std::vector<DrawPacket> out;
    if (!light || !light->on || !light->trans_clear_warm) return out;
    std::vector<DrawPacket> both;
    for (const DrawPacket &p : packets)
        if (!seen || seen->insert(ClearWarmKey{pipe_identity(p), uint8_t(p.depth_write && p.depth_test ? 1 : 0), samples}).second)
            both.push_back(p);
    if (both.empty()) return out;
    apply_clear_fade(both, 0.5f);
    for (const DrawPacket &w : both) {
        const PipeIdentity k = pipe_identity(w);
        if (std::any_of(out.begin(), out.end(), [&](const DrawPacket &q) { return pipe_identity(q) == k; })) continue;
        out.push_back(w);
    }
    return out;
}
float screen_aspect(const fc_frame &f) { return f.screen_w > 0 && f.screen_h > 0 ? float(f.screen_h) / float(f.screen_w) : 0.75f; }
void Renderer::draw_world(uint32_t target, const fc_frame &f, const FrameView *views, uint32_t n, float pull, const float *clear,
                          const SilhouetteStyle *silhouette, const LightingFrame *light) {
    build_packets(f.world, f.world_count, packets_);
    mark_cutaway(f, light, n > 0 ? views[0].eye_model : nullptr, packets_);
    if (dev_.async_pipelines()) {   // every variant the cut may need, queued (once each) before the cone reaches it
        const std::vector<DrawPacket> warm = cutaway_warm_draws(f, light, packets_);
        if (!warm.empty()) dev_.warm_pipelines_background(std::string(), warm, dev_.target_samples(target));
        const std::vector<DrawPacket> clear_warm = clear_fade_warm_draws(light, packets_, &clear_warmed_, dev_.target_samples(target));
        if (!clear_warm.empty()) dev_.warm_pipelines_background(std::string(), clear_warm, dev_.target_samples(target));
    }
    const float *prev = (f.prev_pos && interp_alpha_ < 1.f) ? f.prev_pos : nullptr;
    // Before the silhouette and depth-fill copies: hidden shadow decals leave no copy behind.
    const bool lit = light && light->on;
    if (lit && light->hide_fake_shadows)
        packets_.erase(std::remove_if(packets_.begin(), packets_.end(),
                                      [](const DrawPacket &p) { return (p.light_flags & FC_LF_FAKE_SHADOW) != 0; }), packets_.end());
    if (lit && light->shadows) {
        sel_packets_.clear();
        const uint8_t casts = light->shadows >= 2 ? (FC_LF_CASTER | FC_LF_STATIC) : FC_LF_CASTER;
        for (const DrawPacket &p : packets_) if (p.light_flags & casts) sel_packets_.push_back(p);
        if (!sel_packets_.empty())
            dev_.encode_shadow_map(fit_shadow(shadow_dir_.update(light->dir), light->bounds_min, light->bounds_max, 2048),
                                   f.verts, f.vert_count, sel_packets_.data(), (uint32_t)sel_packets_.size(),
                                   depth_extras(*light, prev, interp_alpha_));
    }
    // Baked once per scene after 15 stable frames; a new area has no AO until its own bake, never another area's.
    LightingFrame lf_ao;
    const LightingFrame *use = lit ? light : nullptr;
    if (lit && light->ao) {
        dev_.prepare_ao();
        bool bounded = true;
        for (int k = 0; k < 3; k++) bounded = bounded && light->bounds_max[k] > light->bounds_min[k];
        const uint64_t bk = bounded ? ao_bounds_key(light->bounds_min, light->bounds_max) : 0;
        AoSceneKey sk(bk);
        sel_packets_.clear();
        for (const DrawPacket &p : packets_)
            if ((p.light_flags & FC_LF_STATIC) && !(p.light_flags & FC_LF_FAKE_SHADOW)) {
                sel_packets_.push_back(p);
                sk.draw(p.vertex_count, p.tex[0], p.cc.slots, p.cc.opts);
            }
        lf_ao = *light;
        lf_ao.ao_ready = false;
        if (bounded && sk.vertices() > 0) {
            // the bake cuts the clip, so a clip change is a new scene (re-bake). Unclipped keys (SM64) are unchanged.
            const bool clipped = light->wclip_lo[3] > 0.5f || light->wclip_hi[3] > 0.5f;
            if (clipped) sk.clip(light->wclip_lo, light->wclip_hi);
            const uint64_t key = sk.key();
            if (ao_sched_.should_bake(key)) {
                const auto t0 = std::chrono::steady_clock::now();
                const AoVolume vol = ao_volume(light->bounds_min, light->bounds_max, kAoMaxDim);
                std::vector<ShadowFit> aviews;
                for (const auto &d : ao_directions(kAoViews)) aviews.push_back(fit_shadow(d.data(), light->bounds_min, light->bounds_max, kAoMapSize));
                const uint32_t slot = ao_sched_.slot_for(key);
                const float radius = light->ao_radius > 0.f && std::isfinite(light->ao_radius) ? light->ao_radius : kAoRadius;
                if (dev_.bake_ao(slot, vol, aviews, radius, f.verts, f.vert_count, sel_packets_.data(), (uint32_t)sel_packets_.size(),
                                 depth_extras(*light, nullptr, 1.f))) {
                    ao_sched_.done(key, bk);
                    ao_bakes_++; ao_last_ = vol;
                } else {
                    ao_failures_++;
                }
                ao_cpu_ms_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            }
            int slot = ao_sched_.use(key);
            if (slot < 0) slot = ao_sched_.latest(bk);
            if (slot >= 0) { lf_ao.ao_ready = true; lf_ao.ao_slot = (uint8_t)slot; }
        }
        use = &lf_ao;
    }
    // Kept while its scene key holds; its bottom is the frame's floor clip, so a lake bed the table clip hides holds up
    // no sheet.
    if (lit && light->liquid && light->water_trim && light->water_count > 0) {
        const uint32_t nw = std::min<uint32_t>(light->water_count, 8u);
        float top = light->water[0][4];
        for (uint32_t i = 1; i < nw; i++) top = std::max(top, light->water[i][4]);
        top += 0.005f * light->liquid_upm;
        const bool bounded = light->bounds_max[1] > light->bounds_min[1];
        float bottom = (bounded ? std::min(light->bounds_min[1], top - 1.f) : top - 1000.f * light->liquid_upm) - 1.f;
        const float clip_floor = light->wclip_lo[1];
        if (light->wclip_lo[3] > 0.5f && std::isfinite(clip_floor) && clip_floor < top && clip_floor > bottom) bottom = clip_floor;
        float window[4], clip[16];
        sel_packets_.clear();
        AoSceneKey sk(0);
        for (const DrawPacket &p : packets_)
            if ((p.light_flags & FC_LF_STATIC) && !(p.light_flags & FC_LF_FAKE_SHADOW) && !(p.light_role & (FC_LR_WATER | FC_LR_LAVA))) {
                sel_packets_.push_back(p);
                sk.draw(p.vertex_count, p.tex[0], p.cc.slots, p.cc.opts);
            }
        if (!sel_packets_.empty() && water_footprint_window(light->water, nw, light->bounds_min, light->bounds_max, window) &&
            water_footprint_clip(window, top, bottom, clip)) {
            const float lo[4] = {window[0], window[1], top, bottom}, hi[4] = {window[2], window[3], 0.f, 0.f};
            sk.clip(lo, hi);
            const uint64_t key = sk.key();
            if (!(foot_have_ && key == foot_key_ && dev_.reuse_water_footprint())) {
                dev_.encode_water_footprint(window, clip, f.verts, f.vert_count, sel_packets_.data(), (uint32_t)sel_packets_.size());
                foot_key_ = key; foot_have_ = true; foot_renders_++;
            }
        } else {
            foot_have_ = false;
        }
    } else if (lit) {
        foot_have_ = false;
    }
    // a clear transition fades the level to transparent (after the shadow map, the AO bake and the water trim,
// which see the level as captured; before the silhouette and the depth fill, which then copy the faded draws).
    if (lit && light->trans_clear > 0.f) apply_clear_fade(packets_, light->trans_clear);
    const size_t n0 = packets_.size();
    if (silhouette) {   // second pass, same encoder: depth Less (behind), no depth write, stencil "not yet blended here"
        for (size_t i = 0; i < n0; i++) {
            const uint8_t owner = packets_[i].focus;
            if (!owner) continue;
            const fc_focus_style *fs = f.focus_styles && owner <= f.focus_style_count ? &f.focus_styles[owner - 1] : nullptr;
            if (fs && fs->rgba[3] == 0) continue;
            DrawPacket s = packets_[i];
            s.cutaway = 0;
            s.silhouette = kSilhouetteFlat; s.focus = 0; s.depth_test = 1; s.depth_write = 0; s.decal = 0; s.blend = 1;
            if (fs) {
                for (int k = 0; k < 4; k++) s.prim[k] = fs->rgba[k] / 255.f;
                if (fs->cutout) s.silhouette = kSilhouetteCutout;
                s.pull = (float)fs->clearance;
            } else {
                std::memcpy(s.prim, silhouette->rgba, sizeof s.prim);
            }
            packets_.push_back(s);
        }
    }
    // With no skybox nothing opaque lies behind lava or water, and the compositor drops pixels without depth. Last, so
    // nothing tests against it; two tiers: alpha >= 0.5, then any alpha where depth is still clear.
    for (uint8_t tier : {kDepthOnlyHalf, kDepthOnlyFaint})
        for (size_t i = 0; i < n0; i++) {
            DrawPacket z = packets_[i];
            z.cutaway = 0;
            if (!z.blend || !z.depth_test || z.depth_write) continue;
            z.depth_only = tier; z.depth_write = 1; z.blend = 0; z.focus = 0;
            packets_.push_back(z);
        }
    dev_.set_lighting(use);
    for (uint32_t i = 0; i < n; i++) {
        ViewConstants vc{};
        std::memcpy(vc.clip_from_model, views[i].clip_from_model.m, sizeof vc.clip_from_model);
        vc.layer = views[i].layer;
        std::memcpy(vc.eye_model, views[i].eye_model, sizeof vc.eye_model);
        const float *ev = views[i].eye_view;
        std::memcpy(vc.eye_view, (ev[0] != 0.f || ev[1] != 0.f || ev[2] != 0.f) ? ev : views[i].eye_model, sizeof vc.eye_view);
        vc.decal_pull = pull;
        vc.decal_slope = views[i].decal_slope;
        std::memcpy(vc.wclip_lo, views[i].wclip_lo, sizeof vc.wclip_lo);
        std::memcpy(vc.wclip_hi, views[i].wclip_hi, sizeof vc.wclip_hi);
        vc.noise_seed = (float)(f.seq % 1024);
        vc.interp_alpha = interp_alpha_; vc.interp_on = prev ? 1u : 0u;
        PassDesc pd{};
        pd.clear = clear != nullptr && i == 0;
        if (clear) std::memcpy(pd.clear_color, clear, sizeof pd.clear_color);
        pd.depth = true;
        pd.viewport = views[i].viewport;
        dev_.encode_pass(target, pd, vc, f.verts, f.vert_count, packets_.data(), (uint32_t)packets_.size(),
                         lit ? f.normals : nullptr, prev);
    }
    dev_.set_lighting(nullptr);
}
void Renderer::draw_table_shadow(uint32_t target, const fc_frame &f, const FrameView *views, uint32_t n, const LightingFrame &lf,
                                 const TableShadowBox &box) {
    if (!lf.on || !box.valid || n == 0) return;
    if (lf.trans_clear >= 1.f) return;
    const bool contact = lf.contact && lf.contact_strength > 0.f;
    if (!contact && !(lf.shadows && lf.table_cast > 0.f)) return;
    if (contact) {
        build_packets(f.world, f.world_count, packets_);
        sel_packets_.clear();
        for (const DrawPacket &p : packets_) {
            if (!(p.light_flags & (FC_LF_STATIC | FC_LF_CASTER)) || (p.light_flags & FC_LF_FAKE_SHADOW)) continue;
            if (p.blend && !(p.cc.opts & FC_CC_TEXTURE_EDGE)) continue;
            sel_packets_.push_back(p);
        }
        // the underside map draws the frame's vertices where the main pass draws them (same rule as draw_world)
// and cuts the level views' world clip
        const DepthExtras dx = depth_extras(lf, (f.prev_pos && interp_alpha_ < 1.f) ? f.prev_pos : nullptr, interp_alpha_);
        dev_.encode_underside_map(underside_desc(box), f.verts, f.vert_count, sel_packets_.data(), (uint32_t)sel_packets_.size(), dx);
    }
    fc_vertex q[6] = {};
    const float c[4][2] = {{box.x0, box.z0}, {box.x0, box.z1}, {box.x1, box.z1}, {box.x1, box.z0}};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int k = 0; k < 6; k++) { q[k].pos[0] = c[idx[k]][0]; q[k].pos[1] = box.ground_y; q[k].pos[2] = c[idx[k]][1]; }
    const DrawPacket p = table_shadow_packet();
    LightingFrame l = lf;
    l.upm = box.size_upm; l.ground_y = box.ground_y;
    if (!contact) l.contact_strength = 0.f;
    if (lf.trans_clear > 0.f) {
        l.contact_strength *= 1.f - lf.trans_clear;
        l.table_cast *= 1.f - lf.trans_clear;
    }
    dev_.set_lighting(&l);
    for (uint32_t i = 0; i < n; i++) {
        ViewConstants vc{};
        std::memcpy(vc.clip_from_model, views[i].clip_from_model.m, sizeof vc.clip_from_model);
        vc.layer = views[i].layer;
        std::memcpy(vc.eye_model, views[i].eye_model, sizeof vc.eye_model);
        PassDesc pd{}; pd.depth = true; pd.viewport = views[i].viewport;
        dev_.encode_pass(target, pd, vc, q, 6, &p, 1);
    }
    dev_.set_lighting(nullptr);
}
void Renderer::draw_overlay_panel(uint32_t target, const fc_frame &f, Viewport viewport) {
    build_packets(f.overlay, f.overlay_count, packets_);
    for (auto &p : packets_) { p.depth_test = p.depth_write = p.decal = 0; p.cull = FC_CULL_NONE; }
    ViewConstants vc{};
    // NDC in, NDC out; z is forced to 0.5 so GL-style [-1, 1] depths are never clipped by Metal's [0, 1].
    vc.clip_from_model[0] = 1.f; vc.clip_from_model[5] = 1.f; vc.clip_from_model[14] = 0.5f; vc.clip_from_model[15] = 1.f;
    PassDesc pd{};
    pd.depth = false;
    pd.viewport = viewport;
    dev_.encode_pass(target, pd, vc, f.verts, f.vert_count, packets_.data(), (uint32_t)packets_.size());
}
namespace {
ar_mat4 quad_matrix(const float o[3], const float r[3], const float u[3], float sx, float ox, float sy, float oy) {
    ar_mat4 m{};
    for (int k = 0; k < 3; k++) {
        m.m[k] = r[k] * sx;
        m.m[4 + k] = u[k] * sy;
        m.m[12 + k] = o[k] + r[k] * ox + u[k] * oy;
    }
    m.m[15] = 1.f;
    return m;
}
float clamp1(float v) { return v < -1.f ? -1.f : (v > 1.f ? 1.f : v); }
}
void overlay_axes(uint8_t flags, const float a[3], const ar_pose &head, float r[3], float u[3], float b[3]) {
    if (flags & FC_GROUP_YAW_ONLY) { ar_yaw_billboard_axes(a, &head, r, u, b); return; }
    const float py[3] = {0.f, 1.f, 0.f};
    float up[3];
    ar_quat_rotate(head.quat, py, up);
    ar_billboard_axes(a, head.pos, up, r, u, b);
}
void Renderer::encode_overlay(uint32_t target, const fc_frame &f, const FrameView *views, uint32_t n, const ar_mat4 &model,
                              const float box[4], const float *backing, bool on_top, const float *depth_box, bool ink) {
    if (backing) {
        float fade = 0.f;
        for (float a : fades_) fade = std::fmax(fade, a);
        fc_vertex q[6] = {};
        const float c[4][2] = {{box[0], box[1]}, {box[2], box[1]}, {box[2], box[3]}, {box[0], box[3]}};
        const int idx[6] = {0, 1, 2, 0, 2, 3};
        for (int k = 0; k < 6; k++) {
            q[k].pos[0] = c[idx[k]][0]; q[k].pos[1] = c[idx[k]][1];
            for (int ch = 0; ch < 4; ch++) q[k].shade[ch] = (uint8_t)std::lround(backing[ch] * 255.f);
        }
        DrawPacket p = backing_packet();
        p.fade = fade;
        for (uint32_t v = 0; v < n; v++) {
            ViewConstants vc{};
            const ar_mat4 cm = ar_mat4_mul(views[v].clip_from_model, model);
            std::memcpy(vc.clip_from_model, cm.m, sizeof vc.clip_from_model);
            vc.layer = views[v].layer;
            PassDesc pd{}; pd.depth = true; pd.viewport = views[v].viewport;
            dev_.encode_pass(target, pd, vc, q, 6, &p, 1);
        }
    }
    build_packets(sel_.data(), (uint32_t)sel_.size(), packets_);
    for (size_t i = 0; i < packets_.size(); i++) {
        DrawPacket &p = packets_[i];
        p.depth_test = on_top ? 0 : 1; p.depth_write = 0; p.decal = 0; p.cull = FC_CULL_NONE; p.blend = 1; p.focus = 0;
        p.fade = fades_[i];
        p.ink = ink ? 1 : 0;
    }
    for (uint32_t v = 0; v < n; v++) {
        ViewConstants vc{};
        const ar_mat4 cm = ar_mat4_mul(views[v].clip_from_model, model);
        std::memcpy(vc.clip_from_model, cm.m, sizeof vc.clip_from_model);
        vc.layer = views[v].layer;
        PassDesc pd{};
        pd.depth = true;
        pd.viewport = views[v].viewport;
        dev_.encode_pass(target, pd, vc, f.verts, f.vert_count, packets_.data(), (uint32_t)packets_.size());
    }
    // A plate colours the whole box, so its depth rectangle is the whole box: a ring with colour and no depth would
    // reproject wrongly.
    const float *db = depth_box && !backing ? depth_box : box;
    depth_quads_.push_back(DepthQuad{model, {db[0], db[1], db[2], db[3]}});
}
// "quads write depth": one depth-only quad over each group's rectangle (NDC box), so the compositor reprojects
// the overlay at its real distance. The colour draws test depth but do not write it. All quads come after all overlay
// colour draws: a rectangle written earlier would cut later groups out, even through its transparent texels.
void Renderer::encode_overlay_depth(uint32_t target, const FrameView *views, uint32_t n) {
    const DrawPacket p = overlay_depth_packet();
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (const DepthQuad &dq : depth_quads_) {
        const float *box = dq.box;
        fc_vertex q[6] = {};
        const float c[4][2] = {{box[0], box[1]}, {box[2], box[1]}, {box[2], box[3]}, {box[0], box[3]}};
        for (int k = 0; k < 6; k++) { q[k].pos[0] = c[idx[k]][0]; q[k].pos[1] = c[idx[k]][1]; }
        for (uint32_t v = 0; v < n; v++) {
            ViewConstants vc{};
            const ar_mat4 cm = ar_mat4_mul(views[v].clip_from_model, dq.model);
            std::memcpy(vc.clip_from_model, cm.m, sizeof vc.clip_from_model);
            vc.layer = views[v].layer;
            PassDesc pd{};
            pd.depth = true;
            pd.viewport = views[v].viewport;
            dev_.encode_pass(target, pd, vc, q, 6, &p, 1);
        }
    }
}
void Renderer::draw_overlays(uint32_t target, const fc_frame &f, const FrameView *views, uint32_t n, const OverlayContext &ctx) {
    depth_quads_.clear();
    gv_drawn_ = false;
    const fc_overlay_group *by_id[256] = {};
    for (uint32_t i = 0; i < f.group_count; i++) by_id[f.groups[i].id] = &f.groups[i];
    sel_.clear(); fades_.clear();
    for (uint32_t i = 0; i < f.overlay_count; i++) {
        const fc_overlay_group *g = by_id[f.overlay[i].group];
        if (g && g->anchor == FC_ANCHOR_PANEL && g->alpha > 0.f) { sel_.push_back(f.overlay[i]); fades_.push_back(g->alpha); }
    }
    if (sel_.empty()) {
        panel_shown_ = false;
    } else {
        if (!panel_shown_) panel_.valid = 0;
        panel_shown_ = true;
        const ar_panel_params pp = AR_PANEL_DEFAULTS;
        float o[3], r[3], u[3], nrm[3];
        ar_panel_update(&panel_, &pp, ctx.head_table.pos, ctx.half_x, ctx.half_z, ctx.dt_s, o, r, u, nrm);
        const float full[4] = {-1.f, -1.f, 1.f, 1.f};
        const float ph = pp.width_m * screen_aspect(f);
        encode_overlay(target, f, views, n, quad_matrix(o, r, u, pp.width_m * 0.5f, 0.f, ph * 0.5f, ph * 0.5f), full,
                       backing_on_ ? backing_ : nullptr, false);
    }
    for (uint32_t gi = 0; gi < f.group_count; gi++) {
        const fc_overlay_group &g = f.groups[gi];
        if (g.anchor != FC_ANCHOR_WORLD_POINT || !(g.alpha > 0.f)) continue;
        if (g.flags & FC_GROUP_GAME_VIEW) { draw_game_view(target, f, views, n, ctx, g); continue; }
        sel_.clear(); fades_.clear();
        float x0 = 1.f, y0 = 1.f, x1 = -1.f, y1 = -1.f;
        for (uint32_t i = 0; i < f.overlay_count; i++) {
            const fc_draw &d = f.overlay[i];
            if (d.group != g.id) continue;
            sel_.push_back(d); fades_.push_back(g.alpha);
            float c[4] = {-1.f, -1.f, 1.f, 1.f};
            if (d.render_flags & FC_RS_CLIP)
                for (int k = 0; k < 4; k++) c[k] = clamp1(d.clip[k]);
            float dx0 = 1.f, dy0 = 1.f, dx1 = -1.f, dy1 = -1.f;
            for (uint32_t v = 0; v < d.vertex_count; v++) {
                const float *p = f.verts[d.first_vertex + v].pos;
                dx0 = std::fmin(dx0, p[0]); dx1 = std::fmax(dx1, p[0]);
                dy0 = std::fmin(dy0, p[1]); dy1 = std::fmax(dy1, p[1]);
            }
            dx0 = std::fmax(dx0, c[0]); dy0 = std::fmax(dy0, c[1]); dx1 = std::fmin(dx1, c[2]); dy1 = std::fmin(dy1, c[3]);
            if (!(dx1 > dx0) || !(dy1 > dy0)) continue;
            x0 = std::fmin(x0, dx0); x1 = std::fmax(x1, dx1);
            y0 = std::fmin(y0, dy0); y1 = std::fmax(y1, dy1);
        }
        if (sel_.empty() || !(x1 > x0) || !(y1 > y0)) continue;
        float depth_box[4] = {x0, y0, x1, y1};
        if (g.flags & FC_GROUP_FIXED_BOX) {
            x0 = clamp1(g.box[0]); y0 = clamp1(g.box[1]); x1 = clamp1(g.box[2]); y1 = clamp1(g.box[3]);
            if (!(x1 > x0) || !(y1 > y0)) continue;
            for (fc_draw &d : sel_) {
                const float c[4] = {-1.f, -1.f, 1.f, 1.f};
                const float *dc = (d.render_flags & FC_RS_CLIP) ? d.clip : c;
                d.clip[0] = std::fmax(clamp1(dc[0]), x0); d.clip[1] = std::fmax(clamp1(dc[1]), y0);
                d.clip[2] = std::fmin(clamp1(dc[2]), x1); d.clip[3] = std::fmin(clamp1(dc[3]), y1);
                d.render_flags |= FC_RS_CLIP;
            }
            depth_box[0] = std::fmax(depth_box[0], x0); depth_box[1] = std::fmax(depth_box[1], y0);
            depth_box[2] = std::fmin(depth_box[2], x1); depth_box[3] = std::fmin(depth_box[3], y1);
            if (!(depth_box[2] > depth_box[0]) || !(depth_box[3] > depth_box[1])) continue;
        }
        const float a[3] = {g.world_pos[0] / ctx.units_per_meter, (g.world_pos[1] - ctx.ground_y) / ctx.units_per_meter,
                            g.world_pos[2] / ctx.units_per_meter};
        const float dx = ctx.head_table.pos[0] - a[0], dy = ctx.head_table.pos[1] - a[1], dz = ctx.head_table.pos[2] - a[2];
        const float w = ar_overlay_width(g.width_m, g.min_angle_rad, std::sqrt(dx * dx + dy * dy + dz * dz));
        const float wpx = (x1 - x0) * 0.5f * (float)f.screen_w, hpx = (y1 - y0) * 0.5f * (float)f.screen_h;
        const float h = w * hpx / wpx;
        float r[3], u[3], b[3];
        overlay_axes(g.flags, a, ctx.head_table, r, u, b);
        const float sx = w / (x1 - x0), sy = h / (y1 - y0);
        const float box[4] = {x0, y0, x1, y1};
        const float *plate = !backing_on_ || !(g.flags & FC_GROUP_BACKING) ? nullptr
                           : (g.flags & FC_GROUP_BACKING_WHITE) ? kPanelBackingWhite : backing_;
        encode_overlay(target, f, views, n, quad_matrix(a, r, u, sx, -sx * 0.5f * (x0 + x1), sy, -sy * y0), box,
                       plate, (g.flags & FC_GROUP_ON_TOP) != 0, depth_box, (g.flags & FC_GROUP_INK) != 0);
    }
    encode_overlay_depth(target, views, n);
    // the offscreen target is freed once no game view has been drawn for kGameViewReleaseFrames
// overlay frames in a row (nothing of this frame sampled it); the next game view makes it again.
    gv_idle_ = gv_drawn_ ? 0 : gv_idle_ + 1;
    if (gv_resident_ && gv_idle_ >= kGameViewReleaseFrames) { dev_.release_target(gv_target_); gv_resident_ = false; gv_have_ = false; }
}
void Renderer::draw_game_view(uint32_t target, const fc_frame &f, const FrameView *views, uint32_t n, const OverlayContext &ctx,
                              const fc_overlay_group &g) {
    const fc_game_view *gv = f.game_view;
    if (!gv || !(gv->aspect > 0.f) || !std::isfinite(gv->aspect) || n == 0) return;
    for (float c : gv->clip_from_level) if (!std::isfinite(c)) return;
    for (float c : gv->eye) if (!std::isfinite(c)) return;
    const float a[3] = {g.world_pos[0] / ctx.units_per_meter, (g.world_pos[1] - ctx.ground_y) / ctx.units_per_meter,
                        g.world_pos[2] / ctx.units_per_meter};
    if (!std::isfinite(a[0]) || !std::isfinite(a[1]) || !std::isfinite(a[2])) return;
    gv_drawn_ = true;
    // Unlit, at the AR target's sample count (the level views' pipelines: nothing new compiles); once per captured
    // frame.
    const float aspect = std::fmin(std::fmax(gv->aspect, 0.25f), 4.f);
    const uint32_t ph = kGameViewHeight, pw = (uint32_t)std::lround((float)ph * aspect);
    const uint32_t samples = dev_.target_samples(target);
    if (!gv_target_) {
        gv_target_ = dev_.create_target(pw, ph, 1, samples);
        dev_.bind_target_texture(kGameViewTexture, gv_target_);
        gv_have_ = false;
    } else if (!gv_resident_ || gv_w_ != pw || gv_h_ != ph || gv_samples_ != samples) {
        dev_.resize_target(gv_target_, pw, ph, 1, samples);
        gv_have_ = false;
    }
    gv_resident_ = true; gv_w_ = pw; gv_h_ = ph; gv_samples_ = samples;
    const bool same = gv_have_ && gv_seq_ == f.seq && std::memcmp(gv_clip_, gv->clip_from_level, sizeof gv_clip_) == 0 &&
                      std::memcmp(gv_clear_, gv->clear_rgba, sizeof gv_clear_) == 0;
    if (!same) {
        PassDesc pd{};
        pd.clear = true;
        for (int k = 0; k < 4; k++) pd.clear_color[k] = gv->clear_rgba[k] / 255.f;
        pd.viewport = Viewport{0.f, 0.f, (float)pw, (float)ph};
        dev_.set_lighting(nullptr);
        sel_.clear();
        for (uint32_t i = 0; i < f.overlay_count; i++) if (f.overlay[i].group == g.id) sel_.push_back(f.overlay[i]);
        build_packets(sel_.data(), (uint32_t)sel_.size(), packets_);
        for (auto &p : packets_) { p.depth_test = p.depth_write = p.decal = 0; p.cull = FC_CULL_NONE; p.focus = 0; }
        ViewConstants sv{};
        sv.clip_from_model[0] = 1.f; sv.clip_from_model[5] = 1.f; sv.clip_from_model[14] = 0.5f; sv.clip_from_model[15] = 1.f;
        pd.depth = false;
        dev_.encode_pass(gv_target_, pd, sv, f.verts, f.vert_count, packets_.data(), (uint32_t)packets_.size());
        build_packets(f.world, f.world_count, packets_);
        ViewConstants vc{};
        std::memcpy(vc.clip_from_model, gv->clip_from_level, sizeof vc.clip_from_model);
        std::memcpy(vc.eye_model, gv->eye, sizeof vc.eye_model);
        std::memcpy(vc.eye_view, gv->eye, sizeof vc.eye_view);
        vc.decal_pull = ctx.decal_pull;
        vc.noise_seed = (float)(f.seq % 1024);
        pd.clear = false;
        pd.depth = true;
        dev_.encode_pass(gv_target_, pd, vc, f.verts, f.vert_count, packets_.data(), (uint32_t)packets_.size());
        gv_renders_++;
        gv_have_ = true; gv_seq_ = f.seq;
        std::memcpy(gv_clip_, gv->clip_from_level, sizeof gv_clip_);
        std::memcpy(gv_clear_, gv->clear_rgba, sizeof gv_clear_);
    }
    const float dx = ctx.head_table.pos[0] - a[0], dy = ctx.head_table.pos[1] - a[1], dz = ctx.head_table.pos[2] - a[2];
    const float w = ar_overlay_width(g.width_m, g.min_angle_rad, std::sqrt(dx * dx + dy * dy + dz * dz));
    const float h = w / aspect;
    float r[3], u[3], b[3];
    overlay_axes(g.flags, a, ctx.head_table, r, u, b);
    const ar_mat4 model = quad_matrix(a, r, u, w * 0.5f, 0.f, h * 0.5f, h * 0.5f);
    fc_vertex q[6] = {};
    const float c[4][2] = {{-1.f, -1.f}, {1.f, -1.f}, {1.f, 1.f}, {-1.f, 1.f}};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int k = 0; k < 6; k++) {
        q[k].pos[0] = c[idx[k]][0]; q[k].pos[1] = c[idx[k]][1];
        q[k].uv0[0] = 0.5f * (c[idx[k]][0] + 1.f); q[k].uv0[1] = 0.5f * (1.f - c[idx[k]][1]);
        for (int ch = 0; ch < 4; ch++) q[k].shade[ch] = 255;
    }
    DrawPacket p = game_view_packet();
    p.depth_test = (g.flags & FC_GROUP_ON_TOP) ? 0 : 1;
    p.fade = std::fmin(g.alpha, 1.f);
    for (uint32_t v = 0; v < n; v++) {
        ViewConstants qv{};
        const ar_mat4 cm = ar_mat4_mul(views[v].clip_from_model, model);
        std::memcpy(qv.clip_from_model, cm.m, sizeof qv.clip_from_model);
        qv.layer = views[v].layer;
        PassDesc qd{}; qd.depth = true; qd.viewport = views[v].viewport;
        dev_.encode_pass(target, qd, qv, q, 6, &p, 1);
    }
    depth_quads_.push_back(DepthQuad{model, {-1.f, -1.f, 1.f, 1.f}});
}
void Renderer::append_ribbon(const float *pts, uint32_t count, const uint8_t rgba[4], float half, const float head[3], bool dashed) {
    for (uint32_t i = 0; i + 1 < count; i++) {
        const float *a = pts + 3 * i, *b = pts + 3 * (i + 1);
        const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const float e[3] = {head[0] - 0.5f * (a[0] + b[0]), head[1] - 0.5f * (a[1] + b[1]), head[2] - 0.5f * (a[2] + b[2])};
        float s[3] = {d[1] * e[2] - d[2] * e[1], d[2] * e[0] - d[0] * e[2], d[0] * e[1] - d[1] * e[0]};
        const float sl = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
        if (!(sl > 1e-9f)) continue;
        for (float &c : s) c *= half / sl;
        uint8_t col[4] = {rgba[0], rgba[1], rgba[2], rgba[3]};
        if (dashed && (i / 8) % 2) for (int k = 0; k < 3; k++) col[k] = (uint8_t)(col[k] * 0.55f);
        const float c4[4][3] = {{a[0] - s[0], a[1] - s[1], a[2] - s[2]}, {a[0] + s[0], a[1] + s[1], a[2] + s[2]},
                                {b[0] + s[0], b[1] + s[1], b[2] + s[2]}, {b[0] - s[0], b[1] - s[1], b[2] - s[2]}};
        const int idx[6] = {0, 1, 2, 0, 2, 3};
        for (int k = 0; k < 6; k++) {
            fc_vertex v{};
            std::memcpy(v.pos, c4[idx[k]], sizeof v.pos);
            std::memcpy(v.shade, col, 4);
            ribbon_.push_back(v);
        }
    }
}
fc_combiner_key Renderer::ribbon_combiner(bool blend) { return shade_combiner(blend); }
void Renderer::warm_ribbon_pipelines(uint32_t samples) {
    std::string err;
    for (bool blend : {false, true}) dev_.compile_combiner(ribbon_combiner(blend), &err, samples);
}
static DrawPacket ribbon_packet(uint32_t first, uint32_t count, bool blend, bool ghost = false) {
    DrawPacket p{};
    p.first_vertex = first; p.vertex_count = count;
    p.cc = Renderer::ribbon_combiner(blend);
    p.depth_test = ghost ? 0 : 1; p.depth_write = ghost ? 0 : 1; p.blend = blend ? 1 : 0; p.cull = FC_CULL_NONE;
    return p;
}
std::vector<DrawPacket> Renderer::fixed_pipeline_draws() {
    std::vector<DrawPacket> v;
    for (uint8_t kind : {kSilhouetteFlat, kSilhouetteCutout})
        for (uint8_t clip : {0, 1}) {   // the silhouette copy (its pipeline ignores the combiner: Metal keys it apart)
            DrawPacket s = ribbon_packet(0, 0, true);
            s.silhouette = kind; s.clip_on = clip;
            v.push_back(s);
        }
    v.push_back(table_shadow_packet());
    v.push_back(overlay_depth_packet());
    for (bool blend : {false, true}) v.push_back(ribbon_packet(0, 0, blend));
    v.push_back(game_view_packet());
    return v;
}
static void ribbon_side(const float *a, const float *b, const float head[3], float s[3], bool *ok) {
    const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float e[3] = {head[0] - 0.5f * (a[0] + b[0]), head[1] - 0.5f * (a[1] + b[1]), head[2] - 0.5f * (a[2] + b[2])};
    s[0] = d[1] * e[2] - d[2] * e[1]; s[1] = d[2] * e[0] - d[0] * e[2]; s[2] = d[0] * e[1] - d[1] * e[0];
    const float sl = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    *ok = sl > 1e-9f;
    if (*ok) for (int k = 0; k < 3; k++) s[k] /= sl;
}
void Renderer::append_strip(const float *pts, uint32_t count, const uint8_t rgba[4], float half, const float head[3]) {
    if (count < 2) return;
    const uint32_t m = count - 1;
    std::vector<std::array<float, 3>> side(m);
    std::vector<bool> ok(m);
    int first_ok = -1;
    for (uint32_t i = 0; i < m; i++) {
        bool g = false;
        ribbon_side(pts + 3 * i, pts + 3 * (i + 1), head, side[i].data(), &g);
        ok[i] = g;
        if (g && first_ok < 0) first_ok = (int)i;
    }
    if (first_ok < 0) return;
    for (uint32_t i = 0; i < m; i++)
        if (!ok[i]) side[i] = i > 0 ? side[i - 1] : side[(size_t)first_ok];
    const float *p0 = pts, *pl = pts + 3 * m;
    const bool closed = m >= 3 && std::fabs(p0[0] - pl[0]) + std::fabs(p0[1] - pl[1]) + std::fabs(p0[2] - pl[2]) < 1e-6f;
    std::vector<std::array<float, 3>> off(count);
    for (uint32_t j = 0; j <= m; j++) {
        const bool has_prev = j > 0 || closed, has_next = j < m || closed;
        const std::array<float, 3> &sn = side[j < m ? j : 0], &sp = side[j > 0 ? j - 1 : m - 1];
        const std::array<float, 3> &one = has_next ? sn : sp;
        float o[3] = {one[0], one[1], one[2]};
        float scale = half;
        if (has_prev && has_next) {
            float mm[3] = {sp[0] + sn[0], sp[1] + sn[1], sp[2] + sn[2]};
            const float ml = std::sqrt(mm[0] * mm[0] + mm[1] * mm[1] + mm[2] * mm[2]);
            if (ml > 1e-6f) {
                for (int k = 0; k < 3; k++) o[k] = mm[k] / ml;
                const float c = o[0] * sn[0] + o[1] * sn[1] + o[2] * sn[2];
                scale = half / std::fmax(c, 0.25f);
            }
        }
        for (int k = 0; k < 3; k++) off[j][k] = o[k] * scale;
    }
    for (uint32_t i = 0; i < m; i++) {
        const float *a = pts + 3 * i, *b = pts + 3 * (i + 1);
        const float *oa = off[i].data(), *ob = off[i + 1].data();
        const float c4[4][3] = {{a[0] - oa[0], a[1] - oa[1], a[2] - oa[2]}, {a[0] + oa[0], a[1] + oa[1], a[2] + oa[2]},
                                {b[0] + ob[0], b[1] + ob[1], b[2] + ob[2]}, {b[0] - ob[0], b[1] - ob[1], b[2] - ob[2]}};
        const int idx[6] = {0, 1, 2, 0, 2, 3};
        for (int k = 0; k < 6; k++) {
            fc_vertex v{};
            std::memcpy(v.pos, c4[idx[k]], sizeof v.pos);
            std::memcpy(v.shade, rgba, 4);
            ribbon_.push_back(v);
        }
    }
}
void Renderer::encode_ribbons(uint32_t target, const DrawPacket *packets, uint32_t np, const FrameView *views, uint32_t n) {
    if (ribbon_.empty() || !np) return;
    for (uint32_t v = 0; v < n; v++) {
        ViewConstants vc{};
        std::memcpy(vc.clip_from_model, views[v].clip_from_model.m, sizeof vc.clip_from_model);
        vc.layer = views[v].layer;
        PassDesc pd{};
        pd.depth = true;
        pd.viewport = views[v].viewport;
        dev_.encode_pass(target, pd, vc, ribbon_.data(), (uint32_t)ribbon_.size(), packets, np);
    }
}
void Renderer::draw_polylines(uint32_t target, const fc_frame &f, const FrameView *views, uint32_t n,
                              const float head[3], float upm) {
    ribbon_.clear();
    for (uint32_t li = 0; li < f.line_count; li++) {
        const fc_polyline &l = f.lines[li];
        if (l.point_count < 2 || l.first_point + l.point_count > f.line_point_count) continue;
        const float half = 0.5f * l.width_m * (upm > 0.f ? upm : 8000.f);
        append_ribbon(f.line_points + 3 * (size_t)l.first_point, l.point_count, l.rgba, half, head, true);
    }
    const DrawPacket p = ribbon_packet(0, (uint32_t)ribbon_.size(), false);
    encode_ribbons(target, &p, 1, views, n);
}
void Renderer::draw_lines(uint32_t target, const std::vector<PreviewLine> &lines, const FrameView *views, uint32_t n, const float head[3]) {
    ribbon_.clear();
    std::vector<DrawPacket> packets;
    for (int solid = 0; solid < 2; solid++) {
        for (const PreviewLine &l : lines) {
            if (l.pts.size() < 6 || (!solid && !l.ghost)) continue;
            const uint32_t first = (uint32_t)ribbon_.size();
            if (solid) {
                append_strip(l.pts.data(), (uint32_t)(l.pts.size() / 3), l.rgba, 0.5f * l.width_m, head);
            } else {
                const uint8_t dim[4] = {l.rgba[0], l.rgba[1], l.rgba[2], (uint8_t)std::lround(l.rgba[3] * kGhostAlpha)};
                append_strip(l.pts.data(), (uint32_t)(l.pts.size() / 3), dim, 0.5f * kGhostWidth * l.width_m, head);
            }
            if (ribbon_.size() > first)
                packets.push_back(ribbon_packet(first, (uint32_t)ribbon_.size() - first, !solid || l.rgba[3] < 255, !solid));
        }
    }
    encode_ribbons(target, packets.data(), (uint32_t)packets.size(), views, n);
}
void Renderer::draw_points(uint32_t target, const std::vector<DebugPoint> &points, const FrameView *views, uint32_t n, const float head[3]) {
    if (points.empty()) return;
    constexpr uint32_t kSegments = 12;
    ribbon_.clear();
    std::vector<DrawPacket> packets;
    std::vector<float> tri;
    for (int solid = 0; solid < 2; solid++) {
        const uint32_t first = (uint32_t)ribbon_.size();
        bool blend = !solid;
        for (const DebugPoint &p : points) {
            const float radius = 0.5f * p.diameter_m * (solid ? 1.f : kGhostWidth);
            const uint8_t col[4] = {p.rgba[0], p.rgba[1], p.rgba[2], solid ? p.rgba[3] : (uint8_t)std::lround(p.rgba[3] * kGhostAlpha)};
            if (solid && p.rgba[3] < 255) blend = true;
            debug_point_disc(p.pos, radius, head, kSegments, &tri);
            for (size_t i = 0; i < tri.size(); i += 3) {
                fc_vertex v{};
                std::memcpy(v.pos, &tri[i], sizeof v.pos);
                std::memcpy(v.shade, col, 4);
                ribbon_.push_back(v);
            }
        }
        if (ribbon_.size() > first) packets.push_back(ribbon_packet(first, (uint32_t)ribbon_.size() - first, blend, !solid));
    }
    encode_ribbons(target, packets.data(), (uint32_t)packets.size(), views, n);
}
}
