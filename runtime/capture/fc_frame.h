#pragma once
#include <stdint.h>
#include "ar_types.h"
#ifdef __cplusplus
extern "C" {
#endif
#define FC_MAGIC 0x46433346u
#define FC_ABI_VERSION 5u   /* 3: per-draw light flags, per-vertex normals; 4: overlay group flags and a fixed crop box; 5: previous positions for render interpolation and the frame's own level info. Later additions within 5 use former padding or are appended: no layout change. */

/* Fixed 32-byte vertex. WORLD: level units (model transform applied, no view/projection).
 * SCREEN: normalized device coordinates of the game's own (ortho) projection. */
typedef struct fc_vertex { float pos[3]; float uv0[2]; float uv1[2]; uint8_t shade[4]; } fc_vertex;

/* Combiner key: 16 operand nibbles in N64-normalized form plus option bits. Nibble index
 * ((cycle * 2 + channel) * 4 + operand), channel 0 = rgb, 1 = alpha, operand a, b, c, d:
 * result = (a - b) * c + d. Values are FC_SRC_*. Cycle 0's COMBINED operands are already 0. */
typedef struct fc_combiner_key { uint64_t slots; uint32_t opts; uint32_t _pad; } fc_combiner_key;
enum {
    FC_SRC_0 = 0, FC_SRC_TEXEL0 = 1, FC_SRC_TEXEL1 = 2, FC_SRC_PRIM = 3, FC_SRC_SHADE = 4, FC_SRC_ENV = 5,
    FC_SRC_TEXEL0A = 6, FC_SRC_LOD = 7, FC_SRC_1 = 8, FC_SRC_TEXEL1A = 9, FC_SRC_COMBINED = 10,
    FC_SRC_COMBINEDA = 11, FC_SRC_PRIMA = 12, FC_SRC_SHADEA = 13, FC_SRC_ENVA = 14, FC_SRC_NOISE = 15
};
enum {
    FC_CC_ALPHA = 1u << 0, FC_CC_FOG = 1u << 1, FC_CC_TEXTURE_EDGE = 1u << 2, FC_CC_NOISE = 1u << 3,
    FC_CC_2CYCLE = 1u << 4, FC_CC_LIGHTMAP = 1u << 5, FC_CC_WORLD_GEOMETRY = 1u << 6
};
enum {
    FC_LF_TINT = 1u << 0,
    FC_LF_BAKED = 1u << 1,
    FC_LF_GAME_LIT = 1u << 2,
    FC_LF_STATIC = 1u << 3,
    FC_LF_CASTER = 1u << 4,
    FC_LF_RECEIVER = 1u << 5,
    FC_LF_FAKE_SHADOW = 1u << 6,
    FC_LF_REFLECTIVE = 1u << 7
};
/* SELF_LIT: the vertex colour already holds the model's complete light, so the room-lit look does not shadow it again
 * by the key light. MIRROR: with FC_LF_REFLECTIVE the draw reflects the room probe; other reflective draws keep the
 * game's map. */
enum { FC_LR_SELF_LIT = 1u, FC_LR_MIRROR = 2u, FC_LR_WATER = 4u, FC_LR_LAVA = 8u };
enum { FC_RS_DEPTH_TEST = 1, FC_RS_DEPTH_WRITE = 2, FC_RS_DECAL = 4, FC_RS_BLEND = 8, FC_RS_CLIP = 16 };
enum { FC_CULL_NONE = 0, FC_CULL_BACK = 1, FC_CULL_FRONT = 2 };
typedef enum { FC_SPACE_WORLD = 0, FC_SPACE_SCREEN = 1 } fc_space_t;
enum { FC_WRAP_REPEAT = 0, FC_WRAP_MIRROR = 1, FC_WRAP_CLAMP = 2 };

typedef struct fc_sampler { uint8_t linear, wrap_s, wrap_t, _pad; } fc_sampler;

typedef struct fc_draw {
    uint32_t first_vertex, vertex_count;
    uint32_t tex[2];                       /* adapter texture ids, 0 = none; 0xFFFFFF00 and up are the renderer's (its
                                            * game view, Renderer::kGameViewTexture): an adapter never uploads them */
    fc_sampler samp[2];
    fc_combiner_key cc;
    uint8_t prim[4], env[4];
    float lod_frac;
    uint8_t space;
    uint8_t render_flags;
    uint8_t cull;
    uint8_t group;
    float viewport[4];                     /* SCREEN: x, y, w, h in game screen pixels */
    float clip[4];                         /* SCREEN with FC_RS_CLIP: the game's scissor as an NDC box x0, y0, x1, y1 */
    uint8_t focus;
    uint8_t light_flags;
    uint8_t light_role;
    uint8_t fade;                          /* WORLD (former padding): 0 = drawn as captured; 1..255 = fading in: drawn blended at fade / 255 of its opacity, with its own depth test and write */
} fc_draw;

enum { FC_ANCHOR_HIDDEN = 0, FC_ANCHOR_WORLD_POINT = 1, FC_ANCHOR_PANEL = 2 };
/* ON_TOP: the colour ignores depth; its depth rectangle is still written. FIXED_BOX: the quad shows the NDC crop `box`,
 * the draws scissored to it. */
enum { FC_GROUP_YAW_ONLY = 1u, FC_GROUP_ON_TOP = 2u, FC_GROUP_FIXED_BOX = 4u, FC_GROUP_BACKING = 8u, FC_GROUP_BACKING_WHITE = 16u,
       FC_GROUP_GAME_VIEW = 32u, FC_GROUP_INK = 64u };
/* A group without an entry is hidden. */
typedef struct fc_overlay_group {
    uint8_t id;
    uint8_t anchor;
    uint8_t flags;
    uint8_t _pad;
    float alpha;
    float world_pos[3];                    /* WORLD_POINT: bottom centre of the quad, level units */
    float width_m;
    float min_angle_rad;
    float box[4];
} fc_overlay_group;
/* The FC_GROUP_GAME_VIEW group's own SCREEN draws (the game's background) are drawn first, behind the world. */
typedef struct fc_game_view { float clip_from_level[16]; float eye[3]; float aspect; uint8_t clear_rgba[4]; } fc_game_view;
/* cutout reads whatever texture 0 the draw binds: set it only for owners whose draws all sample texture 0 (sprites). */
typedef struct fc_focus_style { uint8_t rgba[4]; uint8_t cutout; uint8_t _pad; uint16_t clearance; } fc_focus_style;
typedef struct fc_polyline { uint32_t first_point, point_count; uint8_t rgba[4]; float width_m; } fc_polyline;

/* Applied in the room-lit look only; a frame without one draws exactly as before. Its clips replace the session's world
 * clip for this frame, in every pass. */
#define FC_LOOK_MAX_WATER 8
typedef struct fc_water_box { float lo[2], hi[2]; float surface_y; } fc_water_box;
enum { FC_LOOK_CLIP_FLOOR = 1u, FC_LOOK_CLIP_BOX = 2u, FC_LOOK_LIQUID = 4u, FC_LOOK_CUTAWAY = 8u, FC_LOOK_TRANSITION = 16u,
       FC_LOOK_FINE_TEXTURES = 32u, FC_LOOK_WATER_TRIM = 64u, FC_LOOK_FLOATING = 128u, FC_LOOK_CUT_PREV = 256u,
       FC_LOOK_CUT_ARMED = 512u,     /* the cut is on; warm its variants even with no target (Mario hidden) */
       FC_LOOK_TRANS_CLEAR_ARMED = 1024u };
enum { FC_TRANS_FADE = 1, FC_TRANS_IRIS = 2, FC_TRANS_CLEAR = 3 };
typedef struct fc_look {
    uint32_t flags;
    float clip_floor_y;
    float clip_lo[2], clip_hi[2];
    float time_s;
    uint32_t water_count;
    fc_water_box water[FC_LOOK_MAX_WATER];
    float cut_target[3];
    float cut_radius;
    float trans_cover;
    float trans_centre[3];
    float trans_radius;
    uint8_t trans_kind;
    uint8_t trans_rgb[3];
    float trans_cover_prev;           /* TRANSITION: the previous tick's cover (the renderer mixes toward trans_cover by
                                       * the frame's interpolation alpha; not finite: trans_cover alone) */
    float cut_target_prev[3];
} fc_look;

typedef struct fc_frame {
    uint32_t magic, abi_version;
    uint64_t seq;
    uint32_t screen_w, screen_h;
    const fc_vertex *verts;   uint32_t vert_count;
    const fc_draw *world;     uint32_t world_count;     /* captured order = draw order */
    const fc_draw *overlay;   uint32_t overlay_count;
    uint32_t discarded_draws;
    uint32_t unsupported_draws;
    const fc_overlay_group *groups; uint32_t group_count;
    const float *line_points; uint32_t line_point_count;
    const fc_polyline *lines; uint32_t line_count;
    const int8_t *normals;   /* v3: 4 per vertex (x, y, z as snorm/127, w 0), level space; NULL = none */
    /* v5. prev_pos: where each vertex was one game tick earlier (level units, 3 floats per vertex, SCREEN vertices
     * equal to their current position); the renderer draws mix(prev_pos, pos, alpha). NULL = draw pos as is.
     * has_level / level: the level info this frame was captured with; the runtime builds the frame's views from it, so
     * geometry and scale never come from two different ticks. has_level 0 = the adapter's level_info(). */
    const float *prev_pos;
    uint32_t has_level;
    ar_level_info level;
    /* additive within ABI 5: appended after every v5 field, none moves; a zero-initialised frame has NULL: the game's
     * own camera for an FC_GROUP_GAME_VIEW group; NULL = none (such a group shows nothing). */
    const fc_game_view *game_view;
    /* C (additive within ABI 5: appended after game_view, none moves; a zero-initialised frame has NULL / 0): the
     * silhouette of focus owner i (fc_draw.focus == i + 1, i < focus_style_count); NULL, or an owner past the count: the
     * renderer's own style (flat grey, no cutout), as before. */
    const fc_focus_style *focus_styles;
    uint32_t focus_style_count;
    /* additive within ABI 5: appended after focus_style_count, none moves; a zero-initialised frame has NULL: the
     * game's diorama look; NULL = none: no look applies. */
    const fc_look *look;
} fc_frame;
static inline const fc_overlay_group *fc_frame_find_group(const fc_frame *f, uint8_t id) {
    for (uint32_t i = 0; i < f->group_count; i++) { if (f->groups[i].id == id) { return &f->groups[i]; } }
    return 0;
}
#ifdef __cplusplus
}
#endif
