/* Game thread only. */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "pc/gfx/gfx_rendering_api.h"
#include "pc/gfx/gfx_cc.h"
#include "pc/gfx/gfx_ar_hooks.h"
#include "fc_builder.h"
#include "sm64_capture.h"
#include "sm64_ar_trees.h"
#include "sm64_light.h"
#include "sm64_host.h"
#include "sm64_look.h"
#include "sm64_settings.h"
#include "sm64_ar_internal.h"

_Static_assert(CC_0 == FC_SRC_0 && CC_TEXEL0 == FC_SRC_TEXEL0 && CC_TEXEL1 == FC_SRC_TEXEL1 && CC_PRIM == FC_SRC_PRIM &&
               CC_SHADE == FC_SRC_SHADE && CC_ENV == FC_SRC_ENV && CC_TEXEL0A == FC_SRC_TEXEL0A && CC_LOD == FC_SRC_LOD &&
               CC_1 == FC_SRC_1 && CC_TEXEL1A == FC_SRC_TEXEL1A && CC_COMBINED == FC_SRC_COMBINED &&
               CC_COMBINEDA == FC_SRC_COMBINEDA && CC_PRIMA == FC_SRC_PRIMA && CC_SHADEA == FC_SRC_SHADEA &&
               CC_ENVA == FC_SRC_ENVA && CC_NOISE == FC_SRC_NOISE, "FC_SRC_* mirror gfx_cc.h CC_*");
_Static_assert(GFX_AR_CULL_NONE == FC_CULL_NONE && GFX_AR_CULL_BACK == FC_CULL_BACK && GFX_AR_CULL_FRONT == FC_CULL_FRONT,
               "cull enums match");

struct ShaderProgram {
    struct ShaderProgram *next;
    struct ColorCombiner cc;
    uint8_t num_inputs;
    bool used_tex[2];
    fc_combiner_key key;
};

#define MAX_TRIS 256   /* MAX_BUFFERED in gfx.h */
typedef struct { uint8_t prim[4], env[4]; float lod; } tri_consts;

static struct ShaderProgram *sPrograms, *sCur;
static ar_snapshot_ring *sRing;
static ar_texture_fifo *sFifo;
static fc_builder *sB;
static uint64_t sSeq;
static uint32_t sNextTex = 1, sTile, sBound[2];
static fc_sampler *sTexSamp;         /* sampler state per texture id (GL semantics: it lives on the texture) */
static uint32_t sTexSampCap;
static bool sDepthTest, sDepthWrite, sDecal, sAlpha;
static int sSpace = GFX_AR_SPACE_SCREEN, sCull = GFX_AR_CULL_NONE;
static int sGroups[8], sGroupDepth, sFocus;
_Static_assert(GFX_AR_ROLE_TERRAIN == SM64_ROLE_TERRAIN && GFX_AR_ROLE_OBJECT == SM64_ROLE_OBJECT &&
               GFX_AR_ROLE_SHADOW == SM64_ROLE_SHADOW && GFX_AR_ROLE_EFFECT == SM64_ROLE_EFFECT &&
               GFX_AR_ROLE_GODDARD == SM64_ROLE_GODDARD &&
               GFX_AR_ROLE_WATER == SM64_ROLE_WATER && GFX_AR_ROLE_LAVA == SM64_ROLE_LAVA &&
               GFX_AR_MAT_LIT == SM64_MAT_LIT &&
               GFX_AR_MAT_TEXGEN == SM64_MAT_TEXGEN, "roles and materials match gfx_ar_hooks.h");
static int sRole = SM64_ROLE_TERRAIN, sMat = 0;
static int sRoom, sRoomCover = 255;
static sm64_room_tris sRoomTris[256], sRoomTrisLast[256];
static int sGoddardSelfLit;
static int sActStarSolid;
static float sWMin[3], sWMax[3], sWMinLast[3], sWMaxLast[3];
static int sWHave, sWHaveLast;
static float sViewport[4];
static float sScissor[4];
static void (*sFrameHook)(fc_builder *b);
static sm64_capture_stats sStats;
static fc_vertex sVerts[MAX_TRIS * 3];
static tri_consts sConsts[MAX_TRIS];
static int sTreeOf[MAX_TRIS];
static fc_vertex sExtra[SM64_TREES_EXTRA_MAX];
#define TREE_BATCH_MAX 16
extern uint32_t gGlobalTimer;

static fc_combiner_key make_key(const struct CombineMode *cm) {
    fc_combiner_key k = { 0, 0, 0 };
    for (int i = 0; i < 16; i++) { k.slots |= (uint64_t)(cm->all_values[i] & 0xF) << (4 * i); }
    k.opts = (cm->use_alpha ? FC_CC_ALPHA : 0) | (cm->use_fog ? FC_CC_FOG : 0) | (cm->texture_edge ? FC_CC_TEXTURE_EDGE : 0) |
             (cm->use_dither ? FC_CC_NOISE : 0) | (cm->use_2cycle ? FC_CC_2CYCLE : 0) | (cm->light_map ? FC_CC_LIGHTMAP : 0) |
             (cm->world_geometry ? FC_CC_WORLD_GEOMETRY : 0);
    return k;
}

static bool z_is_from_0_to_1(void) { return false; }
static void unload_shader(struct ShaderProgram *p) { (void)p; }
static void load_shader(struct ShaderProgram *p) { sCur = p; }
static struct ShaderProgram *create_and_load_new_shader(struct ColorCombiner *cc) {
    struct ShaderProgram *p = calloc(1, sizeof *p);
    if (!p) { return NULL; }
    struct CCFeatures f;
    gfx_cc_get_features(cc, &f);
    p->cc = *cc;
    p->num_inputs = (uint8_t)f.num_inputs;
    p->used_tex[0] = f.used_textures[0];
    p->used_tex[1] = f.used_textures[1];
    p->key = make_key(&cc->cm);
    p->next = sPrograms; sPrograms = p; sCur = p;
    return p;
}
static struct ShaderProgram *lookup_shader(struct ColorCombiner *cc) {
    for (struct ShaderProgram *p = sPrograms; p; p = p->next) { if (p->cc.hash == cc->hash) { return p; } }
    return NULL;
}
static void shader_get_info(struct ShaderProgram *p, uint8_t *n, bool used[2]) {
    if (!p) { *n = 0; used[0] = used[1] = false; return; }
    *n = p->num_inputs; used[0] = p->used_tex[0]; used[1] = p->used_tex[1];
}
static void ensure_samp(uint32_t id) {
    if (id < sTexSampCap) { return; }
    uint32_t cap = sTexSampCap ? sTexSampCap : 256;
    while (cap <= id) { cap *= 2; }
    fc_sampler *n = realloc(sTexSamp, cap * sizeof *n);
    if (!n) { return; }
    memset(n + sTexSampCap, 0, (cap - sTexSampCap) * sizeof *n);
    sTexSamp = n; sTexSampCap = cap;
}
static uint32_t new_texture(void) { uint32_t id = sNextTex++; ensure_samp(id); return id; }
static void select_texture(int tile, uint32_t id) { sTile = (uint32_t)tile & 1u; sBound[sTile] = id; }
static void upload_texture(const uint8_t *rgba, int w, int h) {
    sStats.uploads++;
    if (sFifo && sBound[sTile] && w > 0 && h > 0) { ar_texfifo_push(sFifo, sSeq, sBound[sTile], (uint32_t)w, (uint32_t)h, rgba); }
}
static uint8_t wrap_of(uint32_t cm) { return (cm & 2u) ? FC_WRAP_CLAMP : (cm & 1u) ? FC_WRAP_MIRROR : FC_WRAP_REPEAT; }
static void set_sampler_parameters(int tile, bool linear, uint32_t cms, uint32_t cmt) {
    uint32_t id = sBound[tile & 1];
    ensure_samp(id);
    if (id < sTexSampCap) { fc_sampler s = { linear ? 1 : 0, wrap_of(cms), wrap_of(cmt), 0 }; sTexSamp[id] = s; }
}
static void set_depth_test(bool v) { sDepthTest = v; }
static void set_depth_mask(bool v) { sDepthWrite = v; }
static void set_zmode_decal(bool v) { sDecal = v; }
static void set_viewport(int x, int y, int w, int h) { sViewport[0] = (float)x; sViewport[1] = (float)y; sViewport[2] = (float)w; sViewport[3] = (float)h; }
static void set_scissor(int x, int y, int w, int h) { sScissor[0] = (float)x; sScissor[1] = (float)y; sScissor[2] = (float)w; sScissor[3] = (float)h; }
/* SCREEN: the game's scissor as an NDC box of the current viewport (both are bottom-up window pixels). */
static void fill_clip(fc_draw *d) {
    const float vx = sViewport[0], vy = sViewport[1], vw = sViewport[2], vh = sViewport[3];
    if (!(vw > 0.f) || !(vh > 0.f) || !(sScissor[2] > 0.f) || !(sScissor[3] > 0.f)) { return; }
    const float x0 = (sScissor[0] - vx) / vw * 2.f - 1.f, x1 = (sScissor[0] + sScissor[2] - vx) / vw * 2.f - 1.f;
    const float y0 = (sScissor[1] - vy) / vh * 2.f - 1.f, y1 = (sScissor[1] + sScissor[3] - vy) / vh * 2.f - 1.f;
    if (x0 <= -0.999f && y0 <= -0.999f && x1 >= 0.999f && y1 >= 0.999f) { return; }
    d->render_flags |= FC_RS_CLIP;
    d->clip[0] = x0; d->clip[1] = y0; d->clip[2] = x1; d->clip[3] = y1;
}
static void set_use_alpha(bool v) { sAlpha = v; }

static uint8_t f2u8(float f) { if (!(f > 0.f)) { return 0; } if (f >= 1.f) { return 255; } return (uint8_t)(f * 255.f + 0.5f); }

/* buf_vbo layout per vertex (gfx_pc.c gfx_sp_tri1): pos4, [uv0 2], [uv1 2], [fog 4], [lightmap 2], inputs * (3|4). */
static size_t stride_of(const struct ShaderProgram *p) {
    const struct CombineMode *cm = &p->cc.cm;
    return 4 + (p->used_tex[0] ? 2 : 0) + (p->used_tex[1] ? 2 : 0) + (cm->use_fog ? 4 : 0) + (cm->light_map ? 2 : 0)
         + (size_t)p->num_inputs * (cm->use_alpha ? 4 : 3);
}
static void decode(const float *v, const struct ShaderProgram *p, bool world, fc_vertex *o, tri_consts *k) {
    const struct CombineMode *cm = &p->cc.cm;
    float w = v[3];
    if (world || w == 0.f) { o->pos[0] = v[0]; o->pos[1] = v[1]; o->pos[2] = v[2]; }
    else { o->pos[0] = v[0] / w; o->pos[1] = v[1] / w; o->pos[2] = v[2] / w; }
    size_t i = 4;
    memset(o->uv0, 0, sizeof o->uv0); memset(o->uv1, 0, sizeof o->uv1);
    if (p->used_tex[0]) { o->uv0[0] = v[i]; o->uv0[1] = v[i + 1]; i += 2; }
    if (p->used_tex[1]) { o->uv1[0] = v[i]; o->uv1[1] = v[i + 1]; i += 2; }
    if (cm->use_fog) { i += 4; }
    if (cm->light_map) { i += 2; }
    int ch = cm->use_alpha ? 4 : 3;
    uint8_t shade[4] = { 255, 255, 255, 255 };
    for (int j = 0; j < p->num_inputs; j++, i += (size_t)ch) {
        const float *c = &v[i];
        uint8_t r = f2u8(c[0]), g = f2u8(c[1]), b = f2u8(c[2]), a = ch == 4 ? f2u8(c[3]) : 255;
        switch (p->cc.shader_input_mapping[j]) {
            case CC_SHADE:  shade[0] = r; shade[1] = g; shade[2] = b; if (ch == 4) { shade[3] = a; } break;
            case CC_SHADEA: shade[3] = r; break;
            case CC_PRIM:   k->prim[0] = r; k->prim[1] = g; k->prim[2] = b; if (ch == 4) { k->prim[3] = a; } break;
            case CC_PRIMA:  k->prim[3] = r; break;
            case CC_ENV:    k->env[0] = r; k->env[1] = g; k->env[2] = b; if (ch == 4) { k->env[3] = a; } break;
            case CC_ENVA:   k->env[3] = r; break;
            case CC_LOD:    k->lod = c[0]; break;
            default: break;
        }
    }
    memcpy(o->shade, shade, 4);
}
static void fill_proto(fc_draw *d, bool world) {
    memset(d, 0, sizeof *d);
    d->space = world ? FC_SPACE_WORLD : FC_SPACE_SCREEN;
    for (int i = 0; i < 2; i++) {
        if (!sCur->used_tex[i]) { continue; }
        d->tex[i] = sBound[i];
        if (sBound[i] < sTexSampCap) { d->samp[i] = sTexSamp[sBound[i]]; }
    }
    d->cc = sCur->key;
    d->render_flags = (uint8_t)((sDepthTest ? FC_RS_DEPTH_TEST : 0) | (sDepthWrite ? FC_RS_DEPTH_WRITE : 0) |
                                (sDecal ? FC_RS_DECAL : 0) | (sAlpha ? FC_RS_BLEND : 0));
    d->cull = world ? (uint8_t)sCull : FC_CULL_NONE;
    d->group = (!world && sGroupDepth > 0 && sGroupDepth <= 8) ? (uint8_t)sGroups[sGroupDepth - 1] : 0;
    d->focus = world ? (uint8_t)sFocus : 0;
    memcpy(d->viewport, sViewport, sizeof sViewport);
    if (!world) { fill_clip(d); }
    d->light_flags = world ? sm64_light_flags(sRole, sMat, d->render_flags, d->cc.opts) : 0;
    d->light_role = world ? sm64_light_role(sRole, sMat, sFocus > 0 && (sMat & SM64_MAT_TEXGEN) && sm64_host_player_metal(sFocus - 1),
                                            sGoddardSelfLit, sRole == SM64_ROLE_GODDARD && sm64_host_title_splash()) : 0;
}
/* A point sample inside a tile never wraps on the N64, but the renderer reads past the edge (MSAA, mips, HD glyphs),
 * which with G_TX_WRAP fetched the opposite edge's ink: such tiles are sampled clamped. */
static void clamp_tiles_used_inside(fc_draw *d, const fc_vertex *v, size_t n) {
    const float lo = -1e-5f, hi = 1.f + 1e-5f;
    for (int i = 0; i < 2; i++) {
        if (!sCur->used_tex[i] || d->samp[i].linear) { continue; }
        bool in_s = true, in_t = true;
        for (size_t k = 0; k < n && (in_s || in_t); k++) {
            const float *uv = i == 0 ? v[k].uv0 : v[k].uv1;
            in_s = in_s && uv[0] >= lo && uv[0] <= hi;
            in_t = in_t && uv[1] >= lo && uv[1] <= hi;
        }
        if (in_s) { d->samp[i].wrap_s = FC_WRAP_CLAMP; }
        if (in_t) { d->samp[i].wrap_t = FC_WRAP_CLAMP; }
    }
}
static void draw_triangles(float buf[], size_t len, size_t ntris) {
    if (!sB || !sCur || ntris == 0) { return; }
    if (sSpace == GFX_AR_SPACE_DISCARD) { fc_builder_count(sB, 1, 0); return; }
    if (sRoom > 0 && sSpace == GFX_AR_SPACE_WORLD && sRoomCover == 0) {
        sRoomTris[sRoom].hidden += (uint32_t)ntris;
        fc_builder_count(sB, 1, 0);
        return;
    }
    size_t stride = stride_of(sCur);
    if (sCur->cc.cm.light_map || ntris > MAX_TRIS || stride * 3 * ntris != len) { fc_builder_count(sB, 0, 1); return; }
    bool world = sSpace == GFX_AR_SPACE_WORLD;
    for (size_t t = 0; t < ntris; t++) {
        tri_consts k, scratch;
        memset(&k, 0, sizeof k); k.prim[3] = k.env[3] = 255;
        for (int c = 0; c < 3; c++) {
            scratch = k;
            decode(&buf[(t * 3 + (size_t)c) * stride], sCur, world, &sVerts[t * 3 + (size_t)c], c == 0 ? &k : &scratch);
        }
        sConsts[t] = k;
    }
    if (world && gArStory && gArScreen == GFX_AR_SCREEN_TITLE) {
        for (size_t i = 0; i < ntris * 3; i++) {
            const float *p = sVerts[i].pos;
            if (!sWHave) { for (int k = 0; k < 3; k++) { sWMin[k] = sWMax[k] = p[k]; } sWHave = 1; continue; }
            for (int k = 0; k < 3; k++) {
                if (p[k] < sWMin[k]) sWMin[k] = p[k];
                if (p[k] > sWMax[k]) sWMax[k] = p[k];
            }
        }
    }
    const bool trees = world && ntris <= TREE_BATCH_MAX;
    for (size_t t = 0; t < ntris; t++) { sTreeOf[t] = trees ? sm64_trees_match(gGlobalTimer, &sVerts[t * 3]) : -1; }
    fc_draw proto;
    fill_proto(&proto, world);
    clamp_tiles_used_inside(&proto, sVerts, ntris * 3);
    if (world && sRoom > 0) {
        proto.fade = sRoomCover < 255 ? (uint8_t)sRoomCover : 0;
        if (sRoomCover < 255) { sRoomTris[sRoom].fading += (uint32_t)ntris; } else { sRoomTris[sRoom].shown += (uint32_t)ntris; }
        if (sRoomCover == 255 && (sRole == SM64_ROLE_TERRAIN || sRole == SM64_ROLE_WATER || sRole == SM64_ROLE_LAVA)) {
            sRoomTris[sRoom].terrain += (uint32_t)ntris;
        }
    }
    const uint8_t cull = proto.cull;
    size_t start = 0;
    for (size_t t = 1; t <= ntris; t++) {
        if (t == ntris || memcmp(&sConsts[t], &sConsts[start], sizeof(tri_consts)) != 0 || sTreeOf[t] != sTreeOf[start]) {
            memcpy(proto.prim, sConsts[start].prim, 4);
            memcpy(proto.env, sConsts[start].env, 4);
            proto.lod_frac = sConsts[start].lod;
            const int star = !world && sActStarSolid && sm64_act_star_is_pale(proto.group == GFX_AR_GROUP_MENU, proto.render_flags, proto.env);
            if (star) { proto.env[3] = SM64_ACT_STAR_SOLID_ALPHA; }
            const int tree = sTreeOf[start];
            proto.cull = tree >= 0 ? FC_CULL_NONE : cull;
            fc_vertex *dst = fc_builder_add_draw(sB, &proto, (uint32_t)((t - start) * 3));
            memcpy(dst, &sVerts[start * 3], (t - start) * 3 * sizeof(fc_vertex));
            if (star) { sm64_act_star_shade(dst, (uint32_t)((t - start) * 3)); }
            if (tree >= 0) {
                uint32_t n = 0;
                for (size_t k = start; k < t; k++) { n += sm64_trees_extra(tree, &sVerts[k * 3], sExtra); }
                fc_vertex *x = fc_builder_add_draw(sB, &proto, n);
                for (size_t k = start; k < t; k++) {
                    const uint32_t m = sm64_trees_extra(tree, &sVerts[k * 3], sExtra);
                    memcpy(x, sExtra, m * sizeof(fc_vertex));
                    x += m;
                }
            }
            start = t;
        }
    }
}
static void init(void) {}
static void on_resize(void) {}
/* Inside the adapter's tick end_frame holds the frame back and the tick publishes it with its level info, so a scale
 * switch never pairs one tick's geometry with another tick's scale. Outside a tick (boot, tests) frames publish at
 * end_frame with no level. */
static int sDefer;
static fc_builder *sHeld;
static float sWorldOff[3];
void sm64_capture_set_world_offset(const float d[3]) { for (int k = 0; k < 3; k++) { sWorldOff[k] = d[k]; } }
static int sFloating;
void sm64_capture_set_floating(int on) { sFloating = on != 0; }
static void publish_held(const ar_level_info *level) {
    if (!sHeld) { return; }
    if (sFloating) { sm64_ar_look_float(sHeld); }
    if (sWorldOff[0] != 0.f || sWorldOff[1] != 0.f || sWorldOff[2] != 0.f) { fc_builder_translate_world(sHeld, sWorldOff); }
    fc_builder_set_level(sHeld, level);
    ar_ring_publish(sRing);
    sHeld = NULL;
}
static void start_frame(void) {
    if (sHeld) { publish_held(NULL); }
    sSeq++;
    sStats.frames++;
    sSpace = GFX_AR_SPACE_SCREEN;   /* gfx_run resets gfx_pc's side the same way */
    sGroupDepth = 0;
    sFocus = 0;
    sRole = SM64_ROLE_TERRAIN; sMat = 0;
    sRoom = 0; sRoomCover = 255;
    memset(sRoomTris, 0, sizeof sRoomTris);
    sWHave = 0;
    sActStarSolid = gArStory && gArScreen == GFX_AR_SCREEN_ACT_SELECT && sm64_settings_choice("act_star_solid") == 0;
    sB = sRing ? ar_ring_begin_write(sRing) : NULL;
    if (sRing && !sB) { sStats.dropped++; }
    if (sB) { fc_builder_reset(sB, sSeq, 320, 240); }
}
static _Atomic int sNormalFlags;
void sm64_capture_set_normal_flags(uint8_t mask) { atomic_store(&sNormalFlags, (int)mask); }
static void end_frame(void) {
    if (sB) {
        const int nf = atomic_load(&sNormalFlags);
        if (nf) { fc_builder_derive_normals(sB, (uint8_t)nf, 50.f, 1.f); }
        if (sFrameHook) { sFrameHook(sB); }
        memcpy(sWMinLast, sWMin, sizeof sWMin); memcpy(sWMaxLast, sWMax, sizeof sWMax); sWHaveLast = sWHave;
        memcpy(sRoomTrisLast, sRoomTris, sizeof sRoomTris);
        if (sDefer) { sHeld = sB; }
        else { ar_ring_publish(sRing); }
        sB = NULL;
    }
}
static void finish_render(void) {}
static const char *get_name(void) { return "sm64ar capture"; }
static void shutdown(void) {}

static void hook_space(int s) { sSpace = s; }
static void hook_cull(int c) { sCull = c; }
static void hook_group(int begin, int id) {
    if (begin) { if (sGroupDepth < 8) { sGroups[sGroupDepth] = id; } sGroupDepth++; }
    else if (sGroupDepth > 0) { sGroupDepth--; }
}
static void hook_focus(int owner) { sFocus = owner; }
static void hook_role(int r) { sRole = r; }
static void hook_material(int m) { sMat = m; }
static void hook_room(int room) {
    sRoom = room;
    sRoomCover = room > 0 ? sm64_host_room_coverage(room) : 255;
}

static struct GfxRenderingAPI sApi = {
    .z_is_from_0_to_1 = z_is_from_0_to_1, .unload_shader = unload_shader, .load_shader = load_shader,
    .create_and_load_new_shader = create_and_load_new_shader, .lookup_shader = lookup_shader,
    .shader_get_info = shader_get_info, .new_texture = new_texture, .select_texture = select_texture,
    .upload_texture = upload_texture, .set_sampler_parameters = set_sampler_parameters,
    .set_depth_test = set_depth_test, .set_depth_mask = set_depth_mask, .set_zmode_decal = set_zmode_decal,
    .set_viewport = set_viewport, .set_scissor = set_scissor, .set_use_alpha = set_use_alpha,
    .draw_triangles = draw_triangles, .init = init, .on_resize = on_resize, .start_frame = start_frame,
    .end_frame = end_frame, .finish_render = finish_render, .get_name = get_name, .shutdown = shutdown,
};
static struct GfxArHooks sHooks = { hook_space, hook_cull, hook_group, hook_focus, hook_role, hook_material, hook_room };
extern struct GfxRenderingAPI *gRenderApi;   /* pc_main.c: gfx_init(gWindowApi, gRenderApi) runs inside boot */

void sm64_capture_install(ar_snapshot_ring *ring, ar_texture_fifo *fifo) {
    sRing = ring; sFifo = fifo;
    gRenderApi = &sApi;
    gGfxArHooks = &sHooks;
}
sm64_capture_stats sm64_capture_get_stats(void) { return sStats; }
sm64_room_tris sm64_capture_room_tris(int room) {
    sm64_room_tris none = { 0, 0, 0, 0 };
    return room >= 0 && room < 256 ? sRoomTrisLast[room] : none;
}
const void *sm64_capture_rendering_api(void) { return &sApi; }
void sm64_capture_set_frame_hook(void (*fn)(fc_builder *b)) { sFrameHook = fn; }
void sm64_capture_begin_tick(void) { sDefer = 1; }
void sm64_capture_end_tick(const ar_level_info *level) {
    sDefer = 0;
    publish_held(level);
}
int sm64_capture_world_bounds(float mn[3], float mx[3]) {
    if (!sWHaveLast) { return 0; }
    for (int k = 0; k < 3; k++) { mn[k] = sWMinLast[k]; mx[k] = sWMaxLast[k]; }
    return 1;
}

#include "shading_view.h"
void sm64_capture_set_head_rotation(const float quat[4]) {
    float m[4][4];
    sm64_shading_view_from_quat(quat, m);
    gfx_ar_set_shading_view(m);
}
void sm64_capture_set_light_dir(const float dir_level[3]) {
    float m[4][4];
    sm64_light_view_for_dir(dir_level, m);
    gfx_ar_set_light_view(m);
}
void sm64_capture_clear_light_dir(void) { gfx_ar_clear_light_view(); }
void sm64_capture_set_goddard_self_lit(int on) { sGoddardSelfLit = on != 0; }
