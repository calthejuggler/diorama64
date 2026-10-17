#include "sm64_host.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sm64.h"
#include "game/level_update.h"
#include "game/area.h"
#include "game/game_init.h"
#include "pc/cliopts.h"
#include "pc/platform.h"
#include "engine/surface_load.h"
#include "ground_height.h"
#include "surface_terrains.h"
#include "sm64_vis.h"
#include "sm64_look.h"
#include "sm64_eyes.h"
#include "pc/gfx/gfx_ar_hooks.h"
#include "game/ar_game_hooks.h"
#include "game/rendering_graph_node.h"
#include "game/mario_misc.h"
#include <stdatomic.h>
#include <math.h>
#include <dirent.h>
#include <limits.h>
#include "sm64_pack_stamp.h"
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>
#include "game/hardcoded.h"
#include "audio/external.h"
#include "seq_ids.h"
#include "game/ingame_menu.h"
#include "dialog_ids.h"
#include "sm64_story.h"
#include "menu/file_select.h"
#include "menu/star_select.h"
#include "menu/intro_geo.h"
#include "goddard/ar_goddard.h"
#include "sm64_settings.h"
#include "behavior_data.h"
#include "model_ids.h"
#include "game/object_helpers.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"

extern int  sm64coop_boot(int argc, char *argv[]);
extern void sm64coop_tick(void);

static char sError[256];
const char *sm64_host_last_error(void) { return sError; }

static sm64_boot_result install_rom(const char *savepath, const char *rom_path) {
    if (!rom_path) { snprintf(sError, sizeof sError, "no ROM path given"); return SM64_BOOT_NO_ROM; }
    FILE *in = fopen(rom_path, "rb");
    if (!in) { snprintf(sError, sizeof sError, "ROM not found: %s", rom_path); return SM64_BOOT_NO_ROM; }
    fseek(in, 0, SEEK_END); long n = ftell(in); fseek(in, 0, SEEK_SET);
    if (n <= 0) { fclose(in); snprintf(sError, sizeof sError, "not an N64 ROM: %s", rom_path); return SM64_BOOT_BAD_ROM; }
    unsigned char *b = malloc((size_t)n);
    if (!b || fread(b, 1, (size_t)n, in) != (size_t)n) { fclose(in); free(b); snprintf(sError, sizeof sError, "cannot read ROM"); return SM64_BOOT_BAD_ROM; }
    fclose(in);
    if (n >= 4 && b[0] == 0x37 && b[1] == 0x80) {                 /* v64: swap byte pairs */
        for (long i = 0; i + 1 < n; i += 2) { unsigned char t = b[i]; b[i] = b[i + 1]; b[i + 1] = t; }
    } else if (n >= 4 && b[0] == 0x40 && b[1] == 0x12) {          /* n64: reverse 32-bit words */
        for (long i = 0; i + 3 < n; i += 4) { unsigned char t0 = b[i], t1 = b[i + 1]; b[i] = b[i + 3]; b[i + 1] = b[i + 2]; b[i + 2] = t1; b[i + 3] = t0; }
    } else if (!(n >= 4 && b[0] == 0x80 && b[1] == 0x37)) {
        free(b); snprintf(sError, sizeof sError, "not an N64 ROM: %s", rom_path); return SM64_BOOT_BAD_ROM;
    }
    char dst[1024];
    int len = snprintf(dst, sizeof dst, "%s/baserom.us.z64", savepath ? savepath : "");
    if (!savepath || len < 0 || (size_t)len >= sizeof dst) {
        free(b); snprintf(sError, sizeof sError, "cannot write ROM copy: save path missing or too long"); return SM64_BOOT_BAD_ROM;
    }
    FILE *out = fopen(dst, "wb");
    if (!out) { free(b); snprintf(sError, sizeof sError, "cannot write ROM copy: %s", dst); return SM64_BOOT_BAD_ROM; }
    size_t wrote = fwrite(b, 1, (size_t)n, out);
    int closed = fclose(out); free(b);
    if (wrote != (size_t)n || closed != 0) {
        remove(dst);
        snprintf(sError, sizeof sError, "cannot write ROM copy: %s (disk full?)", dst); return SM64_BOOT_BAD_ROM;
    }
    return SM64_BOOT_OK;
}

#include "pc/controller/controller_api.h"
#include "game/object_list_processor.h"
#include "object_constants.h"
extern struct ControllerAPI *gControllerInjection;
extern struct ControllerAPI gControllerAr;
extern void ar_controller_set(const sm64_pad *p);
void sm64_host_set_pad(const sm64_pad *p) { gControllerInjection = &gControllerAr; ar_controller_set(p); }
void sm64_host_pad_seen(sm64_pad *out) {
    struct Controller *c = &gControllers[0];
    out->buttons = c->buttonDown; out->stick_x = (int8_t)c->rawStickX; out->stick_y = (int8_t)c->rawStickY;
}

static int sArMode = 0;
static _Atomic int sStory;
void sm64_host_set_story(int on) { atomic_store(&sStory, on ? 1 : 0); }
int  sm64_host_story(void) { return atomic_load(&sStory); }
static int sScreenNow;
static _Atomic int sScreenPub;
void sm64_host_set_ar_mode(int on) { sArMode = on ? 1 : 0; gArMode = sArMode != 0; }

static char sResourceDir[1024];
void sm64_host_set_resource_dir(const char *dir) {
    if (!dir) { gArResourcePathOverride = NULL; return; }
    snprintf(sResourceDir, sizeof sResourceDir, "%s", dir);
    gArResourcePathOverride = sResourceDir;
}

static char sSavePath[1024];
static void scan_packs(void);
static bool pack_file_allowed(const char *pack_dir, const char *file);
static int sPackStampsRequired;
void sm64_host_set_pack_stamps_required(int on) { sPackStampsRequired = on ? 1 : 0; }
static char sBundledPacks[1024];
void sm64_host_set_bundled_pack_dir(const char *dir) {
    snprintf(sBundledPacks, sizeof sBundledPacks, "%s", dir ? dir : "");
    gArDynosExtraPacksFolder = sBundledPacks[0] ? sBundledPacks : NULL;
}

sm64_boot_result sm64_host_boot(const char *savepath, const char *rom_path) {
    sError[0] = '\0';
    sm64_boot_result r = install_rom(savepath, rom_path);
    if (r != SM64_BOOT_OK) { return r; }
    /* Injected input is exclusive from the first poll: a host-driven run never reads a physical pad. */
    const sm64_pad neutral = { 0, 0, 0 };
    gControllerInjection = &gControllerAr;
    ar_controller_set(&neutral);
    char *argv_level[] = { "sm64ar", "--savepath", (char *)savepath, "--headless", "--hide-loading-screen",
                           "--skip-intro", "--disable-mods", "--skip-update-check", NULL };
    char *argv_story[] = { "sm64ar", "--savepath", (char *)savepath, "--headless", "--hide-loading-screen",
                           "--disable-mods", "--skip-update-check", NULL };
    const int story = sm64_host_story();
    char **argv = story ? argv_story : argv_level;
    const int argc = story ? 7 : 8;
    gArStory = story != 0;
    sScreenNow = story ? SM64_SCREEN_TITLE : SM64_SCREEN_LEVEL;
    gArDynosPacksOnly = true;
    gArDynosPackFileAllowed = sPackStampsRequired ? pack_file_allowed : NULL;   /* refuses every file until scan_packs */
    snprintf(sSavePath, sizeof sSavePath, "%s", savepath);
    if (!sm64coop_boot(argc, argv)) {
        snprintf(sError, sizeof sError, "ROM rejected by the game (not a US SM64 ROM?)");
        return SM64_BOOT_BAD_ROM;
    }
    gArMode = sArMode != 0;
    scan_packs();
    return SM64_BOOT_OK;
}

int sm64_host_input_injected(void) { return gControllerInjection == &gControllerAr; }

int      sm64_host_level(void)        { return gCurrLevelNum; }
int      sm64_host_act(void)          { return gCurrActNum; }
uint32_t sm64_host_mario_action(void) { return gMarioStates[0].action; }
int sm64_host_player_metal(int i) {
    if (i < 0 || i >= MAX_PLAYERS || gMarioStates[i].marioBodyState == NULL) { return 0; }
    return (gMarioStates[i].marioBodyState->modelState & MODEL_STATE_METAL) != 0;
}
void     sm64_host_mario_pos(float out[3]) { out[0] = gMarioStates[0].pos[0]; out[1] = gMarioStates[0].pos[1]; out[2] = gMarioStates[0].pos[2]; }
float    sm64_host_mario_hitbox_height(void) { return gMarioStates[0].marioObj ? gMarioStates[0].marioObj->hitboxHeight : 0.f; }
uint32_t sm64_host_frame_count(void) { return gGlobalTimer; }

#include "pc/network/network.h"
#include "pc/configfile.h"
#include "data/dynos.c.h"
extern struct NetworkSystem *gNetworkSystemOverride;
extern struct NetworkSystem gNetworkSystemArNull;
extern void djui_panel_do_host(bool reconnecting, bool playSound);
extern bool gDjuiDisabled;

#define SM64_PACKS_MAX 32
typedef struct { char name[128]; int kind, tex, bins; double mb; } host_pack;
static host_pack sPacks[SM64_PACKS_MAX];
static sm64_pack_stamp sPackStamp[SM64_PACKS_MAX];
static char sPackReal[SM64_PACKS_MAX][PATH_MAX];
static char sPackRefusal[SM64_PACKS_MAX][192];
static int sPackStampOk, sPackStampRefused;
static double sPackStampMs;
static _Atomic int sPackCount = -1;
static pthread_mutex_t sPackLock = PTHREAD_MUTEX_INITIALIZER;
static int sPackOn[3];
static char sPackWant[3][128];
static int sPackDirty = 1;
static _Atomic double sPackLoadMs;
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec * 1e3 + (double)t.tv_nsec * 1e-6; }
static int ends_with(const char *s, const char *suf) { const size_t a = strlen(s), b = strlen(suf); return a > b && !strcmp(s + a - b, suf); }
static void scan_packs(void) {
    int n = dynos_pack_get_count();
    if (n > SM64_PACKS_MAX) {   /* the rest are never listed, so never enabled (config dynos-pack lines are ignored in AR) */
        printf("AR: %d DynOS packs installed; only the first %d are listed\n", n, SM64_PACKS_MAX);
        n = SM64_PACKS_MAX;
    }
    const double t0 = now_ms();
    int stamped = 0, refused = 0;
    for (int i = 0; i < n; i++) {
        host_pack *p = &sPacks[i];
        const char *name = dynos_pack_get_name(i);
        snprintf(p->name, sizeof p->name, "%s", name ? name : "");
        char dir[1400];
        if (sBundledPacks[0]) { snprintf(dir, sizeof dir, "%s/%s", sBundledPacks, p->name); }
        else { snprintf(dir, sizeof dir, "%s/dynos/packs/%s", sSavePath, p->name); }
        p->tex = p->bins = 0; p->mb = 0.0;
        sPackReal[i][0] = '\0';
        sPackRefusal[i][0] = '\0';
        DIR *d = opendir(dir);
        if (d && sPackStampsRequired) {
            closedir(d);
            d = NULL;
            char why[256];
            if (sm64_pack_stamp_load(dir, &sPackStamp[i], why, sizeof why) && realpath(dir, sPackReal[i])) {
                p->tex = sPackStamp[i].tex; p->bins = sPackStamp[i].bins; p->mb = sPackStamp[i].mb;
                stamped++;
            } else {
                sm64_pack_stamp_free(&sPackStamp[i]);
                sPackReal[i][0] = '\0';
                printf("AR: refusing unstaged pack \"%s\" (%s)\n", p->name, why);
                snprintf(sPackRefusal[i], sizeof sPackRefusal[i], "%s", why[0] ? why : "refused");
                refused++;
            }
        }
        if (d) {
            const struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                const int t = ends_with(e->d_name, ".tex"), b = ends_with(e->d_name, ".bin");
                if (!t && !b) { continue; }
                char f[1700];
                struct stat st;
                snprintf(f, sizeof f, "%s/%s", dir, e->d_name);
                if (stat(f, &st) == 0 && S_ISREG(st.st_mode)) { p->mb += (double)st.st_size / 1048576.0; p->tex += t; p->bins += b; }
            }
            closedir(d);
        }
        p->kind = sm64_packs_kind(p->tex, p->bins);
    }
    sPackStampOk = stamped; sPackStampRefused = refused; sPackStampMs = now_ms() - t0;
    if (sPackStampsRequired) { printf("AR_PACKS_STAMPED ok=%d refused=%d ms=%.1f\n", stamped, refused, sPackStampMs); }
    fflush(stdout);
    sPackCount = n;   /* published after the table */
}
/* DynOS's check before it loads a pack file (gArDynosPackFileAllowed, 0007-DynOS-packs.patch): the folder must be a pack whose stamp
   verified at boot, and the file one it lists. Game thread. */
static bool pack_file_allowed(const char *pack_dir, const char *file) {
    const int n = sPackCount;
    char real[PATH_MAX];
    if (n <= 0 || !pack_dir || !file || !realpath(pack_dir, real)) { return false; }
    for (int i = 0; i < n; i++) {
        if (sPackReal[i][0] && !strcmp(real, sPackReal[i])) { return sm64_pack_stamp_lists(&sPackStamp[i], file) != 0; }
    }
    return false;
}
static int pack_states(sm64_pack_state *st) {
    const int n = sPackCount;
    for (int i = 0; i < n; i++) { st[i].name = sPacks[i].name; st[i].kind = sPacks[i].kind; }
    return n < 0 ? 0 : n;
}
int sm64_host_pack_count(void) { return sPackCount; }
const char *sm64_host_pack_refusal(int i) { return (i >= 0 && i < sPackCount && sPackRefusal[i][0]) ? sPackRefusal[i] : NULL; }
int sm64_host_pack_stamps(int *refused, double *ms) {
    if (!sPackStampsRequired || sPackCount < 0) { return -1; }
    if (refused) { *refused = sPackStampRefused; }
    if (ms) { *ms = sPackStampMs; }
    return sPackStampOk;
}
const char *sm64_host_pack_name(int i) { return (i >= 0 && i < sPackCount) ? sPacks[i].name : NULL; }
int sm64_host_pack_kind(int i) { return (i >= 0 && i < sPackCount) ? sPacks[i].kind : SM64_PACK_NONE; }
int sm64_host_pack_files(int i, int *tex, int *bins, double *mb) {
    if (i < 0 || i >= sPackCount) { return 0; }
    if (tex) { *tex = sPacks[i].tex; }
    if (bins) { *bins = sPacks[i].bins; }
    if (mb) { *mb = sPacks[i].mb; }
    return 1;
}
int sm64_host_pack_enabled(int i) { return (i >= 0 && i < sPackCount) ? (dynos_pack_get_enabled(i) ? 1 : 0) : 0; }
void sm64_host_set_pack_choice(int kind, int on, const char *name) {
    if (kind != SM64_PACK_TEXTURES && kind != SM64_PACK_MODELS) { return; }
    pthread_mutex_lock(&sPackLock);
    sPackOn[kind] = on ? 1 : 0;
    snprintf(sPackWant[kind], sizeof sPackWant[kind], "%s", name ? name : "");
    sPackDirty = 1;
    pthread_mutex_unlock(&sPackLock);
}
int sm64_host_resolve_pack(int kind, int on, const char *name) {
    sm64_pack_state st[SM64_PACKS_MAX];
    const int n = pack_states(st);
    return on ? sm64_packs_pick(st, n, kind, name) : -1;
}
double sm64_host_pack_last_load_ms(void) { return atomic_load_explicit(&sPackLoadMs, memory_order_relaxed); }
/* Game thread, at the start of a tick (gfx and geo state are between frames, as when the coopdx pause menu toggles). */
static void apply_packs(void) {
    if (sPackCount < 0) { return; }
    int on[3]; char want[3][128];
    pthread_mutex_lock(&sPackLock);
    const int dirty = sPackDirty;
    sPackDirty = 0;
    memcpy(on, sPackOn, sizeof on);
    memcpy(want, sPackWant, sizeof want);
    pthread_mutex_unlock(&sPackLock);
    if (!dirty) { return; }
    const double t0 = now_ms();
    sm64_pack_state st[SM64_PACKS_MAX];
    const int n = pack_states(st);
    for (int kind = SM64_PACK_TEXTURES; kind <= SM64_PACK_MODELS; kind++) {
        const int chosen = on[kind] ? sm64_packs_pick(st, n, kind, want[kind]) : -1;
        int plan[SM64_PACKS_MAX];
        sm64_packs_plan(st, n, kind, chosen, plan);
        for (int i = 0; i < n; i++) {
            if (plan[i] >= 0 && plan[i] != (dynos_pack_get_enabled(i) ? 1 : 0)) { dynos_pack_set_enabled(i, plan[i] != 0); }
        }
    }
    atomic_store_explicit(&sPackLoadMs, now_ms() - t0, memory_order_relaxed);
}
static int objects_with_graph(int behavior_id, int *in3d) {
    int n = 0, n3d = 0;
    for (int i = 0; i < OBJECT_POOL_CAPACITY; i++) {
        const struct Object *o = &gObjectPool[i];
        if (!(o->activeFlags & ACTIVE_FLAG_ACTIVE) || !o->header.gfx.sharedChild) { continue; }
        if (behavior_id >= 0 && (!o->behavior || (int)get_id_from_behavior(o->behavior) != behavior_id)) { continue; }
        const struct GraphNode *g = dynos_ar_pack_graph(o->header.gfx.sharedChild);
        n++;
        n3d += ((g ? g : o->header.gfx.sharedChild)->extraFlags & GRAPH_EXTRA_FORCE_3D) != 0;
    }
    if (in3d) { *in3d = n3d; }
    return n;
}
int sm64_host_objects_force_3d(int behavior_id) {
    int n3d = 0;
    objects_with_graph(behavior_id, &n3d);
    return n3d;
}
static _Atomic int sTrees3d;
int sm64_host_trees_3d(void) { return atomic_load_explicit(&sTrees3d, memory_order_relaxed); }
unsigned sm64_host_gfx_clamped(void) { return gArGfxClamped; }
unsigned sm64_host_pack_bin_refusals(void) { return gArDynosBinRefused; }
int sm64_host_probe_layer_clamp(void) { return ar_layer_clamp_probe(); }
unsigned sm64_host_pack_draws(void) { return ar_pack_draws_take(); }
unsigned sm64_host_pack_tilts(void) { return ar_pack_tilts(); }
int sm64_host_pack_tilt_sample(float m[16], float floor_n[3], int *yaw) {
    struct Object *o = NULL;
    Mat4 t;
    if (ar_pack_tilt_last(&o, t) != gGlobalTimer || o == NULL || o->activeFlags == ACTIVE_FLAG_DEACTIVATED) { return 0; }
    struct Surface *floor = NULL;
    find_floor(o->oPosX, o->oPosY + 10.f, o->oPosZ, &floor);
    if (floor == NULL) { return 0; }
    for (int i = 0; i < 16; i++) { m[i] = t[i / 4][i % 4]; }
    floor_n[0] = floor->normal.x; floor_n[1] = floor->normal.y; floor_n[2] = floor->normal.z;
    *yaw = (int)(s16)o->oFaceAngleYaw;
    return 1;
}
static int sMovingCoinAt = -1;
void sm64_host_test_queue_moving_coin(int tick) { sMovingCoinAt = tick; }
static int sMetalCapAt = -1;
void sm64_host_test_queue_metal_cap(int tick) { sMetalCapAt = tick; }
int sm64_host_actor_instances(const char *geo_name) {
    const size_t len = geo_name ? strlen(geo_name) : 0;
    if (len < 5 || len > 64 || strcmp(geo_name + len - 4, "_geo") != 0) { return -1; }
    for (size_t i = 0; i < len; i++) {
        const char c = geo_name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) { return -1; }
    }
    const void *geo = dynos_geolayout_get(geo_name);
    if (!geo) { return -1; }
    int n = 0;
    for (int i = 0; i < OBJECT_POOL_CAPACITY; i++) {
        const struct Object *o = &gObjectPool[i];
        const struct GraphNode *g = o->header.gfx.sharedChild;
        if (!(o->activeFlags & ACTIVE_FLAG_ACTIVE) || !g || g->georef != geo) { continue; }
        const s16 f = o->header.gfx.node.flags;
        n += (f & GRAPH_RENDER_ACTIVE) && !(f & GRAPH_RENDER_INVISIBLE);
    }
    return n;
}
void sm64_host_generate_tex(const char *png_dir, const char *out_dir) { dynos_ar_generate_tex(png_dir, out_dir); }
void sm64_host_generate_actors(const char *dir) { dynos_ar_generate_actors(dir); }

#include "sm64_ar_loop.h"
#include "sm64_ar_hud.h"
#include "sm64_ar_internal.h"
#include "course_table.h"
#include "game/save_file.h"
#include "game/camera.h"
#include "game/sound_init.h"
#include "sm64_levels.h"
_Static_assert(SM64_LV_GROUNDS == LEVEL_CASTLE_GROUNDS && SM64_LV_BOB == LEVEL_BOB && SM64_LV_WF == LEVEL_WF &&
               SM64_LV_JRB == LEVEL_JRB && SM64_LV_CCM == LEVEL_CCM && SM64_LV_BBH == LEVEL_BBH && SM64_LV_HMC == LEVEL_HMC &&
               SM64_LV_LLL == LEVEL_LLL && SM64_LV_SSL == LEVEL_SSL && SM64_LV_DDD == LEVEL_DDD && SM64_LV_SL == LEVEL_SL &&
               SM64_LV_WDW == LEVEL_WDW && SM64_LV_TTM == LEVEL_TTM && SM64_LV_THI == LEVEL_THI && SM64_LV_TTC == LEVEL_TTC &&
               SM64_LV_RR == LEVEL_RR, "sm64_levels.h level numbers match include/level_table.h");
_Static_assert(SM64_LV_CASTLE == LEVEL_CASTLE && SM64_LV_BITDW == LEVEL_BITDW && SM64_LV_VCUTM == LEVEL_VCUTM &&
               SM64_LV_BITFS == LEVEL_BITFS && SM64_LV_SA == LEVEL_SA && SM64_LV_BITS == LEVEL_BITS &&
               SM64_LV_COURTYARD == LEVEL_CASTLE_COURTYARD && SM64_LV_PSS == LEVEL_PSS && SM64_LV_COTMC == LEVEL_COTMC &&
               SM64_LV_TOTWC == LEVEL_TOTWC && SM64_LV_BOWSER_1 == LEVEL_BOWSER_1 && SM64_LV_WMOTR == LEVEL_WMOTR &&
               SM64_LV_BOWSER_2 == LEVEL_BOWSER_2 && SM64_LV_BOWSER_3 == LEVEL_BOWSER_3,
               "sm64_levels.h Secret & Bowser level numbers match include/level_table.h");
_Static_assert(COURSE_BITDW == 16 && COURSE_BITFS == 17 && COURSE_BITS == 18 && COURSE_PSS == 19 && COURSE_COTMC == 20 &&
               COURSE_TOTWC == 21 && COURSE_VCUTM == 22 && COURSE_WMOTR == 23 && COURSE_SA == 24,
               "sm64_levels.c star slots match levels/course_defines.h");
_Static_assert(COURSE_BOB == 1 && COURSE_WF == 2 && COURSE_JRB == 3 && COURSE_CCM == 4 && COURSE_BBH == 5 && COURSE_HMC == 6 &&
               COURSE_LLL == 7 && COURSE_SSL == 8 && COURSE_DDD == 9 && COURSE_SL == 10 && COURSE_WDW == 11 &&
               COURSE_TTM == 12 && COURSE_THI == 13 && COURSE_TTC == 14 && COURSE_RR == 15,
               "sm64_levels.c course numbers match levels/course_defines.h");
_Static_assert(SM64_SCREEN_LEVEL == GFX_AR_SCREEN_LEVEL && SM64_SCREEN_TITLE == GFX_AR_SCREEN_TITLE &&
               SM64_SCREEN_FILE_SELECT == GFX_AR_SCREEN_FILE_SELECT && SM64_SCREEN_ACT_SELECT == GFX_AR_SCREEN_ACT_SELECT &&
               SM64_SCREEN_CAKE == GFX_AR_SCREEN_CAKE, "sm64_story.h screens match patch story flow");
extern f32 sCursorPos[2];
static int compute_screen(void) {
    if (!gArMode) { return SM64_SCREEN_LEVEL; }
    const int s = gArScreen != GFX_AR_SCREEN_LEVEL ? gArScreen : gCurrCreditsEntry != NULL ? SM64_SCREEN_CREDITS : SM64_SCREEN_LEVEL;
    /* CLEAR_LEVEL says LEVEL before any level is up (the boot, between the title and the file select,
     * the act selector and its course): the screen before it stays until the level is ready */
    return s == SM64_SCREEN_LEVEL && !sm64_host_level_ready() ? sScreenNow : s;
}
int sm64_host_screen(void) { return sScreenNow; }
void sm64_host_update_screen(void) { sScreenNow = compute_screen(); }
int sm64_host_menu_scene(void) { return gfx_ar_menu_scene(); }
void sm64_host_set_menu_cursor(float x, float y) { sCursorPos[0] = x; sCursorPos[1] = y; }
int sm64_host_act_select(int i) { return sm64_host_screen() == SM64_SCREEN_ACT_SELECT ? ar_act_selector_select(i) : 0; }
int sm64_host_act_stars_visible(void) { return sm64_host_screen() == SM64_SCREEN_ACT_SELECT ? ar_act_selector_visible_stars() : 0; }
int sm64_host_test_mario_has_model(void) {
    const struct Object *o = gMarioStates[0].marioObj;
    return o != NULL && o->header.gfx.sharedChild != NULL;
}
unsigned sm64_host_level_inits(void) { return gArLevelInits; }
static int graph_has_func(struct GraphNode *n, GraphNodeFunc fn, int depth) {
    if (n == NULL || depth > 16) { return 0; }
    struct GraphNode *c = n;
    do {
        if ((c->type & GRAPH_NODE_TYPE_FUNCTIONAL) && ((struct FnGraphNode *) c)->func == fn) { return 1; }
        if (graph_has_func(c->children, fn, depth + 1)) { return 1; }
        c = c->next;
    } while (c != NULL && c != n);
    return 0;
}
static unsigned sHostTicks;
int sm64_host_title_splash(void) {
    if (sm64_host_screen() != SM64_SCREEN_TITLE || gCurrentArea == NULL || gCurrentArea->root == NULL) { return 0; }
    static unsigned tick = ~0u, inits;
    static const void *area, *root;
    static int splash;
    if (tick != sHostTicks || inits != gArLevelInits || area != gCurrentArea || root != gCurrentArea->root) {
        tick = sHostTicks; inits = gArLevelInits; area = gCurrentArea; root = gCurrentArea->root;
        splash = graph_has_func(&gCurrentArea->root->node, (GraphNodeFunc) geo_intro_super_mario_64_logo, 0);
    }
    return splash;
}
int sm64_host_title_gameover(void) {
    if (sm64_host_screen() != SM64_SCREEN_TITLE || gCurrentArea == NULL || gCurrentArea->root == NULL) { return 0; }
    static unsigned tick = ~0u, inits;
    static const void *area, *root;
    static int over;
    if (tick != sHostTicks || inits != gArLevelInits || area != gCurrentArea || root != gCurrentArea->root) {
        tick = sHostTicks; inits = gArLevelInits; area = gCurrentArea; root = gCurrentArea->root;
        over = graph_has_func(&gCurrentArea->root->node, (GraphNodeFunc) geo_intro_gameover_backdrop, 0);
    }
    return over;
}
static int gd_head_up(void) { return sm64_host_screen() == SM64_SCREEN_TITLE && !sm64_host_title_splash(); }
int sm64_host_gd_world_to_cursor(const float p[3], float out[2]) { return gd_head_up() ? ar_goddard_world_to_cursor(p, out) : 0; }
int sm64_host_gd_cursor_to_world(float out[3]) { return gd_head_up() ? ar_goddard_cursor_to_world(out) : 0; }
int sm64_host_gd_camera_forward(float out[3]) { return gd_head_up() ? ar_goddard_camera_forward(out) : 0; }
void sm64_host_gd_set_cursor(float x, float y) { if (gd_head_up()) { ar_goddard_set_cursor(x, y); } }
unsigned sm64_host_gd_eye_offset(float out[2]) { return gd_head_up() ? ar_goddard_eye_offset(out) : 0; }
unsigned sm64_host_gd_eye_offsets(float left[2], float right[2]) { return gd_head_up() ? ar_goddard_eye_offsets(left, right) : 0; }
int sm64_host_gd_eyes_world(float out[3]) { return gd_head_up() ? ar_goddard_eyes_world(out) : 0; }
unsigned sm64_host_gd_eye_screens(float left[2], float right[2]) { return gd_head_up() ? ar_goddard_eye_screens(left, right) : 0; }
int sm64_host_gd_eyes_still_at(float frame) { return ar_goddard_eyes_still_at(frame) ? 1 : 0; }
int sm64_host_gd_eyes_still_status(void) { return ar_goddard_eyes_still_status(); }
void sm64_host_gd_still_mask(const unsigned char *pose, int n, int run, unsigned char *out) { ar_goddard_still_mask(pose, n, run, out); }
float sm64_host_gd_eye_frame(void) { return ar_goddard_eye_frame(); }
unsigned sm64_host_gd_eye_still_skips(void) { return ar_goddard_eye_still_skips(); }
void sm64_host_gd_set_eye_follow(int on) { ar_goddard_set_eye_follow(on ? 1 : 0); }
void sm64_host_gd_cursor(float out[2]) { if (gd_head_up()) { ar_goddard_get_cursor(out); } else { out[0] = out[1] = 0.f; } }
int  sm64_host_save_file(void) { return gCurrSaveFileNum; }
void sm64_host_menu_cursor(float out[2]) { out[0] = sCursorPos[0]; out[1] = sCursorPos[1]; }
int  sm64_host_demo_running(void) { return gCurrDemoInput != NULL; }
int  sm64_host_mario_in_intro(void) { return gMarioStates[0].action == ACT_INTRO_CUTSCENE; }
int  sm64_host_dialog_id(void) { const s16 id = get_dialog_id(); return id == DIALOG_NONE ? -1 : id; }
extern s8 gDialogBoxType;   /* ingame_menu.c (not in ingame_menu.h); its enum: DIALOG_TYPE_ROTATE 0, DIALOG_TYPE_ZOOM 1 */
int  sm64_host_dialog_inverted(void) { return gDialogBoxType == 1; }
extern s16 sCutsceneShot;
int  sm64_host_intro_shot(void) { return gCamera != NULL && gCamera->cutscene == CUTSCENE_INTRO_PEACH ? sCutsceneShot : -1; }
static struct GraphNode *graph_find_type(struct GraphNode *n, s16 type, int depth) {
    if (n == NULL || depth > 16) { return NULL; }
    struct GraphNode *c = n;
    do {
        if (c->type == type) { return c; }
        struct GraphNode *f = graph_find_type(c->children, type, depth + 1);
        if (f != NULL) { return f; }
        c = c->next;
    } while (c != NULL && c != n);
    return NULL;
}
int sm64_host_game_camera(float aspect, float view[16], float roll[16], float proj[16], float eye[3]) {
    if (gCurrentArea == NULL || gCurrentArea->root == NULL) { return 0; }
    static const void *root;
    static unsigned inits;
    static struct GraphNodeCamera *cam;
    static struct GraphNodePerspective *persp;
    if (root != gCurrentArea->root || inits != gArLevelInits) {
        root = gCurrentArea->root; inits = gArLevelInits;
        cam = (struct GraphNodeCamera *) graph_find_type(&gCurrentArea->root->node, GRAPH_NODE_TYPE_CAMERA, 0);
        persp = (struct GraphNodePerspective *) graph_find_type(&gCurrentArea->root->node, GRAPH_NODE_TYPE_PERSPECTIVE, 0);
    }
    if (cam == NULL || persp == NULL) { return 0; }
    if (cam->pos[0] == cam->focus[0] && cam->pos[1] == cam->focus[1] && cam->pos[2] == cam->focus[2]) { return 0; }
    Mat4 v, r, p;
    mtxf_lookat(v, cam->pos, cam->focus, cam->roll);
    mtxf_rotate_xy(r, cam->rollScreen);
    guPerspectiveF(p, NULL, persp->fov, aspect, (f32) persp->near, (f32) persp->far, 1.0f);
    memcpy(view, v, sizeof v); memcpy(roll, r, sizeof r); memcpy(proj, p, sizeof p);
    for (int k = 0; k < 3; k++) { eye[k] = cam->pos[k]; }
    return 1;
}
static int sStartLevel = 0, sStartAct = 0;
static int sHomeLevel = 0;
static int sReqLevel = 0, sReqAct = 0;
static int sFlightLevel = 0, sFlightTicks = 0, sFlightLanded = 0;
static int sFlightElsewhere = 0;
static int sReqWaitTicks = 0;
static const char *sReqWaitReason = NULL;
static int sForcedCount = 0;
static char sForcedReason[40];
static int sTestHold = 0;
static int has_acts(int course) { return course >= COURSE_MIN && course <= COURSE_STAGES_MAX; }
static int act_for(int level, int act) {
    const int course = get_level_course_num((s16)level);
    if (!has_acts(course)) { return 0; }
    return act >= 1 ? act : sm64_next_act(save_file_get_star_flags(gCurrSaveFileNum - 1, course - 1));
}
static void mark_flight(int level) { sFlightLevel = level; sFlightTicks = 0; sFlightLanded = 0; sFlightElsewhere = 0; }
void sm64_host_mark_warp(int level) { mark_flight(level); }
void sm64_host_test_warp_node(int node) {
    sSourceWarpNodeId = (s16)node; sDelayedWarpArg = 0; sDelayedWarpTimer = 1; sDelayedWarpOp = WARP_OP_WARP_DOOR;
}

int sm64_host_start_level(int level, int act) {
    gCLIOpts.headless = false;          /* headless *server* would make the local player inactive */
    gDjuiDisabled = true;
    configAmountOfPlayers = 1;
    gNetworkSystemOverride = &gNetworkSystemArNull;
    djui_panel_do_host(false, false);
    if (gArMode && !sm64_host_story()) { gServerSettings.stayInLevelAfterStar = 0; }
    if (sm64_host_story()) {
        extern s16 gChangeLevelTransition;
        /* the host's level change is cancelled, both halves: djui_panel_do_host -> fake_lvl_init_from_save_file sets
         * gChangeLevel = entryLevel too (level_update.c:2066); left set, it survives the title and file select (no
         * update_level runs there) and fires at the grounds' first update_level: a hidden reload that restarts Peach's
         * intro */
        gChangeLevelTransition = -1;
        gChangeLevel = -1;
        sStartLevel = 0; sHomeLevel = 0;
        return 1;
    }
    sStartLevel = level;
    sStartAct = act >= 1 ? act : SM64_HOST_NEXT_ACT;
    sHomeLevel = level;
    return 1;
}
int sm64_host_start_bob(int act) { return sm64_host_start_level(LEVEL_BOB, act); }
int sm64_host_request_warp(int level, int act) {
    if (level <= 0) { return 0; }
    if (level != sReqLevel || act != sReqAct) { sReqWaitTicks = 0; sReqWaitReason = NULL; }
    sReqLevel = level; sReqAct = act;
    return 1;
}
static void clear_request(void) { sReqLevel = 0; sReqAct = 0; sReqWaitTicks = 0; sReqWaitReason = NULL; }
int sm64_host_request_forced(const char **reason) {
    if (reason) { *reason = sForcedReason; }
    return sForcedCount;
}
void sm64_host_test_hold_unsafe(int kind) {
    sTestHold = kind;
    if (kind == SM64_HOST_HOLD_NONE) { sDelayedWarpOp = WARP_OP_NONE; sDelayedWarpTimer = 0; }
}
int sm64_host_request_wait(const char **reason) {
    if (reason) { *reason = sReqLevel ? sReqWaitReason : NULL; }
    return sReqLevel ? sReqWaitTicks : 0;
}
int sm64_host_warp_pending(void) { return sStartLevel != 0 || sReqLevel != 0 || sFlightLevel != 0; }
int sm64_host_home_level(void) { return sHomeLevel; }
int sm64_host_course(void) { return gCurrCourseNum; }
int sm64_host_level_ready(void) {
    const struct Object *o = gMarioStates[0].marioObj;
    return gCurrLevelNum != 0 && o != NULL && (o->activeFlags & ACTIVE_FLAG_ACTIVE) && gCurrentArea != NULL &&
           gCurrentArea->index == gCurrAreaIndex && sCurrPlayMode != PLAY_MODE_CHANGE_LEVEL;
}
const char *sm64_host_warp_wait_reason(void) {
    const struct Object *o = gMarioStates[0].marioObj;
    if (sStartLevel) { return "start warp not landed"; }
    if (sFlightLevel) { return "warp in flight"; }
    if (gCurrLevelNum == 0) { return "no level loaded"; }
    if (o == NULL || !(o->activeFlags & ACTIVE_FLAG_ACTIVE)) { return "Mario not spawned"; }
    if (gCurrentArea == NULL || gCurrentArea->index != gCurrAreaIndex) { return "area loading"; }
    if (sCurrPlayMode == PLAY_MODE_CHANGE_LEVEL) { return "level changing"; }
    if (gMarioStates[0].health < 0x100) { return "Mario is dying"; }
    if (sDelayedWarpOp != WARP_OP_NONE) { return "a warp is starting"; }
    if (gWarpTransition.isActive) { return "warp transition"; }
    if (sCurrPlayMode != PLAY_MODE_NORMAL && sCurrPlayMode != PLAY_MODE_PAUSED) { return "game busy"; }
    return NULL;
}
const char *sm64_host_screen_refusal(int screen) { return screen == SM64_SCREEN_LEVEL ? NULL : sm64_screen_name(screen); }
static _Atomic int sRestartReq;
static int sRestartWait;
static _Atomic int sDroppedCount;
static _Atomic(const char *) sDroppedWhat, sDroppedWhy;
static void dropped(const char *what, int screen) {
    atomic_store(&sDroppedWhat, what); atomic_store(&sDroppedWhy, sm64_screen_name(screen));
    atomic_fetch_add(&sDroppedCount, 1);
}
int sm64_host_dropped(const char **what, const char **why) {
    const int n = atomic_load(&sDroppedCount);
    if (what) { *what = n ? atomic_load(&sDroppedWhat) : NULL; }
    if (why) { *why = n ? atomic_load(&sDroppedWhy) : NULL; }
    return n;
}
void sm64_host_test_publish_screen(int screen) { atomic_store(&sScreenPub, screen); }
int  sm64_host_test_skip_intro(void) { return gServerSettings.skipIntro; }
int  sm64_host_test_stay_in_level_after_star(void) { return gServerSettings.stayInLevelAfterStar; }
void sm64_host_test_set_stay_in_level_after_star(int v) { gServerSettings.stayInLevelAfterStar = (enum StarExitType)v; }
int sm64_host_play_from_start(const char **why) {
    const int s = atomic_load(&sScreenPub);
    const char *w = s == SM64_SCREEN_TITLE || s == SM64_SCREEN_FILE_SELECT ? "already at the start"
                  : s == SM64_SCREEN_ACT_SELECT || s == SM64_SCREEN_CAKE ? sm64_screen_name(s) : NULL;
    if (why) { *why = w; }
    if (w) { return 0; }
    atomic_store(&sRestartReq, 1);
    return 1;
}
static void leave_unsafe_state(void) {
    sDelayedWarpOp = WARP_OP_NONE; sDelayedWarpTimer = 0;
    gWarpTransition.isActive = FALSE;
    sFlightLevel = 0; sStartLevel = 0;
    if (gMarioStates[0].health < 0x100) { gMarioStates[0].health = 0x880; gMarioStates[0].hurtCounter = 0; }
    if (sCurrPlayMode != PLAY_MODE_NORMAL && sCurrPlayMode != PLAY_MODE_PAUSED) { set_play_mode(PLAY_MODE_NORMAL); }
}
extern s32 gCourseDoneMenuTimer, gCourseCompleteCoins;
extern s8 gCourseCompleteCoinsEqual;
static void close_pause(void) {   /* as 0008-Story-mode-and-game-flow.patch when the host takes the pause menu's exit */
    if (sCurrPlayMode == PLAY_MODE_PAUSED) {
        raise_background_noise(1);
        gCameraMovementFlags &= ~CAM_MOVE_PAUSE_SCREEN;
        set_play_mode(PLAY_MODE_NORMAL);
    }
}
static void close_menu(void) {
    if (gMenuMode != -1) {   /* a menu still up (the course-done screen and its save menu): closed as its own choice does,
                              * with no choice made (nothing saved); the warp's level load clears the time stop */
        level_set_transition(0, NULL);
        gDialogBoxState = 0;
        gMenuMode = -1;
        gCourseDoneMenuTimer = 0;
        gCourseCompleteCoins = 0;
        gCourseCompleteCoinsEqual = 0;
        gHudFlash = 0;
        gSaveOptSelectIndex = 0;
    }
}
static void apply_request(int force) {
    const int level = sReqLevel, act = act_for(level, sReqAct);
    clear_request();
    if (force) { leave_unsafe_state(); }
    close_pause();
    if (dynos_warp_to_level(level, 1, act)) {
        close_menu();
        sHomeLevel = level; mark_flight(level);
        if (sm64_host_story()) {
            sm64_host_set_story(0); gArStory = false;
            if (gArMode) { gServerSettings.stayInLevelAfterStar = 0; }
        }
    }
}
/* Play from the start: the story turns on and the level loop returns warp_special's -2 (level_main_scripts_entry →
 * goto_mario_head_regular: the title head, then the file select). */
static void apply_restart(int force) {
    sRestartWait = 0;
    clear_request();
    if (force) { leave_unsafe_state(); }
    close_pause();
    sm64_host_set_story(1); gArStory = sArMode != 0;
    sHomeLevel = 0; sStartLevel = 0;
    /* a Level boot passed --skip-intro (skipIntro TRUE); the story's new file gets Peach's letter, Lakitu
     * and the pipe as after a Story boot (level_update.c reads it at the grounds' init and at Mario's spawn) */
    gServerSettings.skipIntro = FALSE;
    if (gCurrCreditsEntry != NULL) {
        gCurrCreditsEntry = NULL;
        sound_banks_enable(SEQ_PLAYER_SFX, SOUND_BANKS_ALL);   /* WARP_OP_CREDITS_NEXT disabled them all */
    }
    warp_special(SPECIAL_WARP_GODDARD);
    close_menu();
}

int sm64_host_using_null_network(void) { return gNetworkSystem == &gNetworkSystemArNull; }

static int sForceHudFlags = -1;
void sm64_host_test_force_hud_flags(int flags) { sForceHudFlags = flags; }
extern u8 gOverrideHideHud;
void sm64_host_test_hide_hud(int on) { gOverrideHideHud = on ? 1 : 0; }
void sm64_host_test_start_timer(void) { level_control_timer(TIMER_CONTROL_SHOW); level_control_timer(TIMER_CONTROL_START); }
/* gArGameHooks->render_hud. render_hud only calls it when gHudDisplay.flags is not HUD_DISPLAY_NONE, but in a course
 * update_hud_values sets the coin flag again earlier in the same tick: a forced value is applied again here, where
 * render_hud reads it, with render_hud's HUD_DISPLAY_NONE gate. */
void sm64_host_render_hud(void) {
    if (sForceHudFlags >= 0) {
        gHudDisplay.flags = (s16)sForceHudFlags;
        if (gHudDisplay.flags == HUD_DISPLAY_NONE) { return; }
    }
    sm64_ar_hud_render();
}

static void rooms_tick(void);
static void object_rooms_tick(void);
static s32 object_room_fallback(struct Object *o);
void sm64_host_tick(void) {
    sHostTicks++;
    apply_packs();
    if (sForceHudFlags >= 0) { gHudDisplay.flags = (s16)sForceHudFlags; }
    gArDrawAllRooms = sm64_host_draw_all_rooms() != 0;   /* the settings, at a tick boundary (any thread may change them) */
    gArOtherRoomObjects = sm64_host_other_room_objects();
    gArHideOutside = sm64_host_hide_outside() != 0;
    gArCannonVisible = sm64_host_cannon_visible() != 0;
    gArPackFloorTilt = sm64_settings_choice("pack_tilt") == 0;
    gArSplashLift = sm64_settings_choice("splash_lift") == 0;
    { static const float kHoldMin[3] = SM64_SPLASH_HOLD_MIN; gArSplashFloorY = kHoldMin[1]; }
    gArStory = sArMode && sm64_host_story();
    gArPipSky = sArMode && sm64_ar_pip_sky_wanted();   /* 0010-HUD-menus-and-screen-scenes.patch: render-only */
    sm64coop_tick();
    if (sMovingCoinAt >= 0 && (int)sHostTicks == sMovingCoinAt && gMarioStates[0].marioObj != NULL) {
        struct Object *mo = gMarioStates[0].marioObj;
        struct Object *c = spawn_object(mo, MODEL_YELLOW_COIN, bhvMovingYellowCoin);
        f32 x = mo->oPosX + 400.f * sins(mo->oFaceAngleYaw), z = mo->oPosZ + 400.f * coss(mo->oFaceAngleYaw), y = mo->oPosY + 150.f;
        for (int d = 400, found = 0; c != NULL && !found && d <= 3000; d += 100) {
            for (int k = 0; k < 8 && !found; k++) {
                const s16 a = (s16)(mo->oFaceAngleYaw + k * 0x2000);
                const f32 px = mo->oPosX + d * sins(a), pz = mo->oPosZ + d * coss(a);
                struct Surface *f = NULL;
                const f32 h = find_floor(px, mo->oPosY + 300.f, pz, &f);
                if (f != NULL && f->normal.y > 0.6f && f->normal.y < 0.97f) { x = px; z = pz; y = h + 20.f; found = 1; }
            }
        }
        if (c != NULL) { c->oPosX = x; c->oPosY = y; c->oPosZ = z; }
    }
    if (sMetalCapAt >= 0 && (int)sHostTicks >= sMetalCapAt && gMarioStates[0].marioObj != NULL) {
        gMarioStates[0].flags |= MARIO_METAL_CAP | MARIO_CAP_ON_HEAD;
        gMarioStates[0].capTimer = 30000;
        sMetalCapAt = -1;
    }
    sScreenNow = compute_screen();
    {
        static int tree_id = -2;
        if (tree_id == -2) { tree_id = sm64_host_behavior_id("bhvTree"); }
        int n3d = 0;
        const int trees = tree_id >= 0 ? objects_with_graph(tree_id, &n3d) : 0;
        atomic_store_explicit(&sTrees3d, sm64_packs_trees_3d(trees, n3d), memory_order_relaxed);
    }
    if (sStartLevel && gCurrLevelNum != 0 && gMarioStates[0].marioObj != NULL) {
        if (dynos_warp_to_level(sStartLevel, 1, act_for(sStartLevel, sStartAct))) { mark_flight(sStartLevel); sStartLevel = 0; }
    }
    /* landed: its level ready for 2 ticks. Never blocks forever: the game ready in another level for over 60 ticks (a warp
     * another warp overtook, or one that never left), or 600 ticks in all, end it. */
    if (sFlightLevel) {
        const int ready = sm64_host_level_ready();
        sFlightLanded = (gCurrLevelNum == sFlightLevel && ready) ? sFlightLanded + 1 : 0;
        sFlightElsewhere = (gCurrLevelNum != sFlightLevel && ready) ? sFlightElsewhere + 1 : 0;
        if (sFlightLanded >= 2 || sFlightElsewhere > 60 || ++sFlightTicks > 600) { sFlightLevel = 0; }
    }
    if (sTestHold == SM64_HOST_HOLD_DELAYED_WARP) { sDelayedWarpOp = WARP_OP_WARP_DOOR; sDelayedWarpTimer = 1000; }
    else if (sTestHold == SM64_HOST_HOLD_PLAY_MODE) { set_play_mode(PLAY_MODE_CHANGE_AREA); }
    if (atomic_exchange(&sRestartReq, 0)) { sRestartWait = 1; }
    if (sRestartWait) {
        const char *why = sm64_host_warp_wait_reason();
        const int s = sm64_host_screen();
        if (s == SM64_SCREEN_TITLE || s == SM64_SCREEN_FILE_SELECT) { sRestartWait = 0; }
        else if (s == SM64_SCREEN_ACT_SELECT || s == SM64_SCREEN_CAKE) { sRestartWait = 0; dropped("play from the start", s); }
        else if (!why) { apply_restart(0); }
        else if (++sRestartWait > SM64_HOST_FORCE_TICKS) { apply_restart(1); }
    }
    if (sReqLevel) {
        if (sm64_host_screen_refusal(sm64_host_screen())) {
            clear_request(); dropped("pick", sm64_host_screen());
        }
        else {
            const char *why = sm64_host_warp_wait_reason();
            if (!why) { apply_request(0); }
            else if (++sReqWaitTicks > SM64_HOST_FORCE_TICKS) {
                snprintf(sForcedReason, sizeof sForcedReason, "%s", why); sForcedCount++;
                apply_request(1);
            } else { sReqWaitReason = why; }
        }
    }
    rooms_tick();
    object_rooms_tick();
    if (sArMode) { sm64_ar_hud_end_tick(); }
    atomic_store(&sScreenPub, sm64_host_screen());
}

/* Static surfaces only: walk gStaticSurfacePartition (dynamic object surfaces live in a separate
 * partition). A surface spans several cells, so floors are de-duplicated by pointer. */
static int cmp_ptr(const void *a, const void *b) {
    const struct Surface *x = *(const struct Surface *const *)a, *y = *(const struct Surface *const *)b;
    return (x > y) - (x < y);
}

static int collect_static(int list, struct Surface ***out) {
    size_t cap = 4096, n = 0;
    struct Surface **v = malloc(cap * sizeof *v);
    if (!v) { *out = NULL; return -1; }
    for (int z = 0; z < NUM_CELLS; z++) for (int x = 0; x < NUM_CELLS; x++) {
        for (struct SurfaceNode *nd = gStaticSurfacePartition[z][x][list].next; nd; nd = nd->next) {
            if (!nd->surface) continue;
            if (n == cap) {
                struct Surface **nv = realloc(v, cap * 2 * sizeof *v);
                if (!nv) { free(v); *out = NULL; return -1; }
                v = nv; cap *= 2;
            }
            v[n++] = nd->surface;
        }
    }
    qsort(v, n, sizeof *v, cmp_ptr);
    size_t m = 0;
    for (size_t i = 0; i < n; i++) if (i == 0 || v[i] != v[i - 1]) v[m++] = v[i];
    *out = v; return (int)m;
}

static const gh_override sGroundOverrides[] = {
    { -1, -1, 0.f }
};

static int is_void_floor(s16 type) {
    return type == SURFACE_DEATH_PLANE || type == SURFACE_VERTICAL_WIND || type == SURFACE_WARP ||
           (type >= SURFACE_INSTANT_WARP_1B && type <= SURFACE_INSTANT_WARP_1E);
}
static float ground(int table_rule) {
    float ov;
    if (gh_override_lookup(sGroundOverrides, (int)(sizeof sGroundOverrides / sizeof sGroundOverrides[0]) - 1,
                           gCurrLevelNum, gCurrAreaIndex, &ov)) { return ov; }
    struct Surface **s; int n = collect_static(SPATIAL_PARTITION_FLOORS, &s);
    if (n < 0) { return 0.f; }
    gh_tri *t = malloc(sizeof(gh_tri) * (size_t)(n + 1)); int k = 0;
    if (!t) { free(s); return 0.f; }
    for (int i = 0; i < n; i++) {
        if (s[i]->normal.y <= 0.01f || is_void_floor(s[i]->type)) continue;
        gh_tri g = {{s[i]->vertex1[1], s[i]->vertex2[1], s[i]->vertex3[1]},
                    {s[i]->vertex1[0], s[i]->vertex2[0], s[i]->vertex3[0]},
                    {s[i]->vertex1[2], s[i]->vertex2[2], s[i]->vertex3[2]}};
        t[k++] = g;
    }
    const float fallback = n > 0 && gMarioStates[0].marioObj && gMarioStates[0].floorHeight > GH_VOID_Y ? gMarioStates[0].floorHeight : 0.f;
    float r = table_rule ? gh_table_ground(t, k, 50.f, fallback) : gh_ground_height(t, k, 50.f);
    free(t); free(s);
    return r;
}
float sm64_host_ground_height(void) { return ground(1); }
static int entry_skip(int type) {
    return is_void_floor((s16)type) || type == SURFACE_INTANGIBLE || type == SURFACE_CAMERA_BOUNDARY || type == SURFACE_RAYCAST;
}
static int entry_void(int type) { return is_void_floor((s16)type); }
static int entry_force(int type) { return surface_has_force((s16)type) ? 1 : 0; }
int sm64_host_level_entry(float pos[3], int *area) {
    if (gArLevelEntryArea <= 0 || gArLevelEntryArea >= MAX_AREAS) { return 0; }
    for (int k = 0; k < 3; k++) { pos[k] = gArLevelEntryPos[k]; }
    *area = gArLevelEntryArea;
    return 1;
}
int sm64_host_entry_ground(float *out_y) {
    float p[3]; int area;
    if (!sm64_host_level_entry(p, &area) || !gAreas || !gAreas[area].terrainData) { return 0; }
    float y;
    if (!gh_terrain_floor_under(gAreas[area].terrainData, p[0], p[1], p[2], entry_skip, entry_force, &y)) { return 0; }
    const int n = gh_terrain_floors(gAreas[area].terrainData, entry_void, entry_force, NULL, 0);
    gh_tri *t = n > 0 ? malloc(sizeof *t * (size_t)n) : NULL;
    if (t) { gh_terrain_floors(gAreas[area].terrainData, entry_void, entry_force, t, n); }
    y = gh_entry_height(p[1], y);
    *out_y = t ? gh_snap_to_plane(y, t, n, 50.f) : y;
    free(t);
    return 1;
}
int sm64_host_area_ground(int rule, float *out_y) {
    if (!out_y || !gAreas || gCurrAreaIndex <= 0 || gCurrAreaIndex >= MAX_AREAS || !gAreas[gCurrAreaIndex].terrainData) { return 0; }
    const int16_t *terrain = gAreas[gCurrAreaIndex].terrainData;
    if (rule == SM64_ANCHOR_BOTTOM) { return gh_terrain_bottom(terrain, entry_void, entry_force, out_y); }
    const int n = gh_terrain_floors(terrain, entry_void, entry_force, NULL, 0);
    gh_tri *t = n > 0 ? malloc(sizeof *t * (size_t)n) : NULL;
    if (!t) { return 0; }
    gh_terrain_floors(terrain, entry_void, entry_force, t, n);
    float y = NAN;
    if (rule == SM64_ANCHOR_LOWEST_FLOOR) {
        y = gh_table_ground(t, n, 50.f, NAN);
    } else {
        y = gh_floor_percentile(t, n, GH_BASE_PERCENTILE, NAN);
        if (isfinite(y)) { y = gh_snap_to_plane(y, t, n, 50.f); }
    }
    free(t);
    if (!isfinite(y)) { return 0; }
    *out_y = y;
    return 1;
}
int sm64_host_area_lowest_floor(float *out_y) {
    if (!out_y || !gAreas || gCurrAreaIndex <= 0 || gCurrAreaIndex >= MAX_AREAS || !gAreas[gCurrAreaIndex].terrainData) { return 0; }
    const int16_t *terrain = gAreas[gCurrAreaIndex].terrainData;
    const int n = gh_terrain_floors(terrain, entry_void, entry_force, NULL, 0);
    gh_tri *t = n > 0 ? malloc(sizeof *t * (size_t)n) : NULL;
    if (!t) { return 0; }
    gh_terrain_floors(terrain, entry_void, entry_force, t, n);
    const float y = gh_lowest_floor(t, n, NAN);
    free(t);
    if (!isfinite(y)) { return 0; }
    *out_y = y;
    return 1;
}
extern s16 gWarpTransDelay;
extern u8 sTransitionColorFadeCount[4];     /* screen_transition.c: the warp fade's counter is index 0 (area.c render_game) */
void sm64_host_warp_sample(struct sm64_warp_sample *out) {
    memset(out, 0, sizeof *out);
    out->active = gWarpTransition.isActive != 0;
    out->type = gWarpTransition.type;
    out->time = gWarpTransition.time;
    out->count = sTransitionColorFadeCount[0];
    out->delay = gWarpTransDelay;
    out->paused = gWarpTransition.pauseRendering != 0;
    out->rgb[0] = gWarpTransition.data.red; out->rgb[1] = gWarpTransition.data.green; out->rgb[2] = gWarpTransition.data.blue;
    out->ready = sm64_host_screen() == SM64_SCREEN_LEVEL && sm64_host_level_ready();
    out->key = (int)((sm64_host_level_inits() * 64u + (unsigned)(gCurrLevelNum & 63)) * 8u + (unsigned)(gCurrAreaIndex & 7));
}
int sm64_host_water_boxes(float *out5, int cap) {
    if (!gEnvironmentRegions || gEnvironmentRegionsLength <= 0) { return 0; }
    if (gCurrLevelNum == LEVEL_LLL && gCurrAreaIndex == 2) { return 0; }   /* the volcano: its one box is the lava floor */
    const s16 *p = gEnvironmentRegions;
    const int n = p[0];
    int got = 0;
    for (int i = 0; i < n; i++) {
        if ((i + 1) * 6 >= gEnvironmentRegionsLength) { break; }   /* as geo_movtex_draw_water_regions */
        const s16 *r = &p[1 + i * 6];
        if (r[0] >= 50) { continue; }
        if (out5 && got < cap) { float *o = &out5[got * 5]; o[0] = r[1]; o[1] = r[2]; o[2] = r[3]; o[3] = r[4]; o[4] = r[5]; }
        got++;
    }
    return got;
}
static _Atomic int sDrawAllRooms = 1;
void sm64_host_set_draw_all_rooms(int on) { sDrawAllRooms = on ? 1 : 0; }
int sm64_host_draw_all_rooms(void) { return sDrawAllRooms; }
_Static_assert(SM64_OTHER_ROOMS_HIDDEN == GFX_AR_OTHER_ROOMS_HIDDEN && SM64_OTHER_ROOMS_FROZEN == GFX_AR_OTHER_ROOMS_FROZEN &&
               SM64_OTHER_ROOMS_LIVE == GFX_AR_OTHER_ROOMS_LIVE, "other-room modes match gfx_ar_hooks.h");
static _Atomic int sOtherRooms = SM64_OTHER_ROOMS_LIVE;
void sm64_host_set_other_room_objects(int mode) {
    if (mode >= SM64_OTHER_ROOMS_HIDDEN && mode <= SM64_OTHER_ROOMS_LIVE) { sOtherRooms = mode; }
}
int sm64_host_other_room_objects(void) { return sOtherRooms; }
static _Atomic int sHideOutside = 1;
void sm64_host_set_hide_outside(int on) { sHideOutside = on ? 1 : 0; }
int sm64_host_hide_outside(void) { return sHideOutside; }
static _Atomic int sCannonVisible = 1;
void sm64_host_set_cannon_visible(int on) { sCannonVisible = on ? 1 : 0; }
int sm64_host_cannon_visible(void) { return sCannonVisible; }
float sm64_host_ground_plane(void) { return ground(0); }

void sm64_host_level_bounds(float mn[3], float mx[3]) {
    for (int k = 0; k < 3; k++) { mn[k] = INFINITY; mx[k] = -INFINITY; }
    for (int list = 0; list < 3; list++) {
        struct Surface **s; int n = collect_static(list, &s);
        if (n < 0) { for (int k = 0; k < 3; k++) { mn[k] = mx[k] = 0.f; } return; }
        for (int i = 0; i < n; i++) {
            s16 *v[3] = { s[i]->vertex1, s[i]->vertex2, s[i]->vertex3 };
            for (int j = 0; j < 3; j++) for (int k = 0; k < 3; k++) {
                if (v[j][k] < mn[k]) mn[k] = v[j][k];
                if (v[j][k] > mx[k]) mx[k] = v[j][k];
            }
        }
        free(s);
    }
}

int sm64_host_static_solids(float min_top, struct sm64_solid **out) {
    *out = NULL;
    size_t n = 0, cap = 0;
    sm64_solid *v = NULL;
    for (int list = 0; list < 3; list++) {
        struct Surface **s; const int m = collect_static(list, &s);
        if (m < 0) { free(v); return -1; }
        for (int i = 0; i < m; i++) {
            const s16 *p[3] = { s[i]->vertex1, s[i]->vertex2, s[i]->vertex3 };
            sm64_solid b = { p[0][0], p[0][2], p[0][0], p[0][2], p[0][1] };
            for (int j = 1; j < 3; j++) {
                if (p[j][0] < b.x0) b.x0 = p[j][0];
                if (p[j][0] > b.x1) b.x1 = p[j][0];
                if (p[j][2] < b.z0) b.z0 = p[j][2];
                if (p[j][2] > b.z1) b.z1 = p[j][2];
                if (p[j][1] > b.top) b.top = p[j][1];
            }
            if (!(b.top > min_top)) continue;
            if (n == cap) {
                const size_t nc = cap ? cap * 2 : 1024;
                sm64_solid *nv = realloc(v, nc * sizeof *nv);
                if (!nv) { free(v); free(s); return -1; }
                v = nv; cap = nc;
            }
            v[n++] = b;
        }
        free(s);
    }
    *out = v;
    return (int)n;
}

static uint64_t fnv(uint64_t h, const void *d, size_t n) {
    const unsigned char *b = d; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; } return h;
}
extern uint16_t ar_get_random_seed(void);   /* 0002-Library-seams-and-host-hooks.patch */
uint64_t sm64_host_state_hash(void) {
    uint64_t h = 1469598103934665603ull;
    struct MarioState *m = &gMarioStates[0];
    h = fnv(h, m->pos, sizeof m->pos); h = fnv(h, m->vel, sizeof m->vel);
    h = fnv(h, &m->action, sizeof m->action); h = fnv(h, &m->faceAngle, sizeof m->faceAngle);
    h = fnv(h, &m->health, sizeof m->health); h = fnv(h, &m->numCoins, sizeof m->numCoins);
    for (int i = 0; i < OBJECT_POOL_CAPACITY; i++) {
        struct Object *o = &gObjectPool[i];
        if (o->activeFlags & ACTIVE_FLAG_ACTIVE) {
            float f[7] = { o->oPosX, o->oPosY, o->oPosZ, o->oVelX, o->oVelY, o->oVelZ, o->oForwardVel };
            int32_t i32[5] = { o->oAction, o->oTimer, o->oFaceAnglePitch, o->oFaceAngleYaw, o->oFaceAngleRoll };
            uint32_t area_timer = o->areaTimer;
            h = fnv(h, f, sizeof f); h = fnv(h, i32, sizeof i32); h = fnv(h, &area_timer, sizeof area_timer);
        }
    }
    uint16_t seed = ar_get_random_seed(); h = fnv(h, &seed, sizeof seed);
    uint32_t timers[2] = { gGlobalTimer, gNetworkAreaTimer };
    h = fnv(h, timers, sizeof timers);
    return h;
}

extern struct MarioBodyState gBodyStates[];
uint64_t sm64_host_deep_state_hash(void) {
    uint64_t h = sm64_host_state_hash();
    for (int i = 0; i < OBJECT_POOL_CAPACITY; i++) {
        const struct Object *o = &gObjectPool[i];
        if (!(o->activeFlags & ACTIVE_FLAG_ACTIVE)) { continue; }
        const struct GraphNodeObject *g = &o->header.gfx;
        const int32_t id = o->behavior ? (int32_t)get_id_from_behavior(o->behavior) : -1;
        h = fnv(h, &i, sizeof i); h = fnv(h, &id, sizeof id);
        h = fnv(h, &o->activeFlags, sizeof o->activeFlags);
        h = fnv(h, &o->rawData, sizeof o->rawData);
        h = fnv(h, o->transform, sizeof o->transform);
        h = fnv(h, g->pos, sizeof g->pos); h = fnv(h, g->angle, sizeof g->angle); h = fnv(h, g->scale, sizeof g->scale);
        h = fnv(h, &g->node.flags, sizeof g->node.flags);
        const int32_t anim[6] = { g->animInfo.animID, g->animInfo.animFrame, g->animInfo.animFrameAccelAssist,
                                  g->animInfo.animAccel, g->animInfo.animTimer, g->animInfo.animYTrans };
        h = fnv(h, anim, sizeof anim);
    }
    const struct MarioState *m = &gMarioStates[0];
    const int32_t mi[] = { m->action, m->prevAction, m->actionArg, m->actionTimer, m->actionState, m->flags, m->input,
                           m->numCoins, m->numStars, m->numLives, m->health, m->hurtCounter, m->healCounter, m->cap,
                           m->capTimer, m->invincTimer, m->squishTimer, m->wallKickTimer, m->doubleJumpTimer,
                           m->framesSinceA, m->framesSinceB, m->faceAngle[0], m->faceAngle[1], m->faceAngle[2],
                           m->angleVel[0], m->angleVel[1], m->angleVel[2], m->slideYaw, m->twirlYaw, m->intendedYaw,
                           m->collidedObjInteractTypes, m->particleFlags, m->floorAngle, m->waterLevel, m->currentRoom,
                           m->heldObj ? (int32_t)(m->heldObj - gObjectPool) : -1 };
    const float mf[] = { m->pos[0], m->pos[1], m->pos[2], m->vel[0], m->vel[1], m->vel[2], m->slideVelX, m->slideVelZ,
                         m->forwardVel, m->peakHeight, m->intendedMag, m->quicksandDepth, m->minimumBoneY,
                         m->ceilHeight, m->floorHeight, m->curAnimOffset };
    h = fnv(h, mi, sizeof mi); h = fnv(h, mf, sizeof mf);
    h = fnv(h, &gBodyStates[0], sizeof gBodyStates[0]);
    const int32_t g[3] = { gMarioCurrentRoom, gDoorCurrentRoom, gAreaUpdateCounter };
    h = fnv(h, g, sizeof g);
    for (int p = 0; p < 3; p++) {
        uint8_t misc[AR_MARIO_MISC_STATE_BYTES];
        ar_mario_misc_state_save((uint8_t)p, misc);
        h = fnv(h, misc, sizeof misc);
    }
    const int16_t floorIntangible = (int16_t)gFindFloorIncludeSurfaceIntangible;
    h = fnv(h, &floorIntangible, sizeof floorIntangible);
    return h;
}

#include <PR/os_cont.h>
#include "sm64_pad_map.h"
_Static_assert(SM64_N64_A == A_BUTTON && SM64_N64_B == B_BUTTON && SM64_N64_Z == Z_TRIG && SM64_N64_START == START_BUTTON &&
               SM64_N64_L == L_TRIG && SM64_N64_R == R_TRIG && SM64_N64_CU == U_CBUTTONS && SM64_N64_CD == D_CBUTTONS &&
               SM64_N64_CL == L_CBUTTONS && SM64_N64_CR == R_CBUTTONS, "N64 button bits match include/PR/os_cont.h");

#include "game/camera.h"
extern void gfx_start_frame(void);
extern void gfx_end_frame_render(void);
extern void send_display_list(struct SPTask *spTask);
void sm64_host_rerender(void) { gfx_start_frame(); send_display_list(gGfxSPTask); gfx_end_frame_render(); }
int  sm64_host_area(void) { return gCurrAreaIndex; }
void sm64_host_camera_pos(float out[3]) { out[0] = gLakituState.pos[0]; out[1] = gLakituState.pos[1]; out[2] = gLakituState.pos[2]; }
void sm64_host_camera_focus(float out[3]) { for (int k = 0; k < 3; k++) { out[k] = gLakituState.focus[k]; } }
int sm64_host_collision_floors(float *tris9, int cap) {
    struct Surface **s; int n = collect_static(SPATIAL_PARTITION_FLOORS, &s);
    if (n < 0) { return 0; }
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (s[i]->normal.y <= 0.01f) continue;
        if (k < cap) {
            s16 *v[3] = { s[i]->vertex1, s[i]->vertex2, s[i]->vertex3 };
            for (int j = 0; j < 3; j++) for (int a = 0; a < 3; a++) tris9[k * 9 + j * 3 + a] = v[j][a];
        }
        k++;
    }
    free(s);
    return k;
}
typedef struct { float area; float c[3]; } vis_floor;
static int cmp_floor_area(const void *a, const void *b) {
    const float x = ((const vis_floor *)a)->area, y = ((const vis_floor *)b)->area;
    return (x < y) - (x > y);
}
int sm64_host_visibility(const float eye[3], int *mario_visible, float *floor_fraction) {
    *mario_visible = 0; *floor_fraction = 0.f;
    struct Surface **lists[3] = { NULL, NULL, NULL }; int counts[3] = { 0, 0, 0 }, total = 0;
    for (int l = 0; l < 3; l++) { counts[l] = collect_static(l, &lists[l]); if (counts[l] > 0) total += counts[l]; }
    sm64_vis_tri *tris = total > 0 ? malloc(sizeof *tris * (size_t)total) : NULL;
    vis_floor *floors = total > 0 ? malloc(sizeof *floors * (size_t)total) : NULL;
    int nt = 0, nf = 0;
    for (int l = 0; l < 3 && tris && floors; l++) {
        for (int i = 0; i < counts[l]; i++) {
            const struct Surface *s = lists[l][i];
            const s16 *v[3] = { s->vertex1, s->vertex2, s->vertex3 };
            sm64_vis_tri *t = &tris[nt++];
            for (int k = 0; k < 3; k++) { t->a[k] = v[0][k]; t->b[k] = v[1][k]; t->c[k] = v[2][k]; }
            t->n[0] = s->normal.x; t->n[1] = s->normal.y; t->n[2] = s->normal.z;
            if (s->normal.y > 0.01f) {
                const double e1[3] = { t->b[0] - t->a[0], t->b[1] - t->a[1], t->b[2] - t->a[2] };
                const double e2[3] = { t->c[0] - t->a[0], t->c[1] - t->a[1], t->c[2] - t->a[2] };
                const double cx = e1[1] * e2[2] - e1[2] * e2[1], cy = e1[2] * e2[0] - e1[0] * e2[2], cz = e1[0] * e2[1] - e1[1] * e2[0];
                vis_floor *f = &floors[nf++];
                f->area = (float)(0.5 * sqrt(cx * cx + cy * cy + cz * cz));
                for (int k = 0; k < 3; k++) f->c[k] = (t->a[k] + t->b[k] + t->c[k]) / 3.f + (k == 1 ? 20.f : 0.f);
            }
        }
    }
    int samples = 0, seen = 0;
    if (tris && floors) {
        qsort(floors, (size_t)nf, sizeof *floors, cmp_floor_area);
        samples = nf < 256 ? nf : 256;
        for (int i = 0; i < samples; i++) seen += !sm64_vis_blocked(tris, nt, eye, floors[i].c);
        float m[3]; sm64_host_mario_gfx_pos(m); m[1] += 80.f;
        *mario_visible = gMarioStates[0].marioObj && !sm64_vis_blocked(tris, nt, eye, m);
        *floor_fraction = samples ? (float)seen / (float)samples : 0.f;
    }
    for (int l = 0; l < 3; l++) free(lists[l]);
    free(tris); free(floors);
    return samples;
}

#include "behavior_table.h"
#include "engine/graph_node.h"
int sm64_host_rendered_objects(float *xyz, int *ids, int cap) {
    int n = 0;
    for (int i = 0; i < OBJECT_POOL_CAPACITY; i++) {
        struct Object *o = &gObjectPool[i];
        if (!(o->activeFlags & ACTIVE_FLAG_ACTIVE) || o->header.gfx.sharedChild == NULL) continue;
        if (!(o->header.gfx.node.flags & GRAPH_RENDER_ACTIVE) || (o->header.gfx.node.flags & GRAPH_RENDER_INVISIBLE)) continue;
        if (n < cap) {
            xyz[n * 3 + 0] = o->header.gfx.pos[0]; xyz[n * 3 + 1] = o->header.gfx.pos[1]; xyz[n * 3 + 2] = o->header.gfx.pos[2];
            ids[n] = (int)get_id_from_behavior(o->behavior);
        }
        n++;
    }
    return n;
}
int sm64_host_king_bobomb_pos(float out[3]) {
    for (int i = 0; i < OBJECT_POOL_CAPACITY; i++) {
        struct Object *o = &gObjectPool[i];
        if ((o->activeFlags & ACTIVE_FLAG_ACTIVE) && get_id_from_behavior(o->behavior) == id_bhvKingBobomb) {
            out[0] = o->header.gfx.pos[0]; out[1] = o->header.gfx.pos[1]; out[2] = o->header.gfx.pos[2];
            return 1;
        }
    }
    return 0;
}
extern bool draw_distance_far_plane_unbounded(void);
int sm64_host_far_plane_unbounded(void) { return draw_distance_far_plane_unbounded(); }
int sm64_host_draw_distance(void) { return (int)configDrawDistance; }
int sm64_host_dialog_open(void) { return get_dialog_id() != DIALOG_NONE; }
int sm64_host_dialog_state(void) { return get_dialog_id() != DIALOG_NONE ? (int)gDialogBoxState : -1; }
void sm64_host_mario_gfx_pos(float out[3]) {
    struct Object *o = gMarioStates[0].marioObj;
    for (int k = 0; k < 3; k++) { out[k] = o ? o->header.gfx.pos[k] : 0.f; }
}
int sm64_host_mario_drawn(void) {
    const struct Object *o = gMarioStates[0].marioObj;
    return o && (o->header.gfx.node.flags & GRAPH_RENDER_ACTIVE) ? 1 : 0;
}
static int sTestNoMarioSource;
void sm64_host_test_no_mario_source(int on) { sTestNoMarioSource = on != 0; }
const void *sm64_host_mario_sound_source(void) {
    if (sTestNoMarioSource || !gMarioStates[0].marioObj) { return NULL; }
    return (const void *)gMarioStates[0].marioObj->header.gfx.cameraToObject;
}
void sm64_host_mario_camera_to_object(float out[3]) {
    struct Object *o = gMarioStates[0].marioObj;
    for (int k = 0; k < 3; k++) { out[k] = o ? o->header.gfx.cameraToObject[k] : 0.f; }
}
int sm64_host_process_lods(void) { return gBehaviorValues.ProcessLODs ? 1 : 0; }
int sm64_host_behavior_id(const char *name) {
    for (int id = 0; id < (int)id_bhv_max_count; id++) {
        const char *n = get_behavior_name_from_id((enum BehaviorId)id);
        if (n && strcmp(n, name) == 0) { return id; }
    }
    return -1;
}
int sm64_host_objects_with_behavior(int behavior_id, float *xyz, int cap) {
    int n = 0;
    for (int i = 0; i < OBJECT_POOL_CAPACITY; i++) {
        struct Object *o = &gObjectPool[i];
        if (!(o->activeFlags & ACTIVE_FLAG_ACTIVE) || (int)get_id_from_behavior(o->behavior) != behavior_id) { continue; }
        if (n < cap) { for (int k = 0; k < 3; k++) { xyz[n * 3 + k] = o->header.gfx.pos[k]; } }
        n++;
    }
    return n;
}
int sm64_host_mario_room(void) { return gMarioCurrentRoom; }

/* Game thread only. The capture asks, from gfx_run after this frame's room switch set gMarioCurrentRoom and
 * gDoorCurrentRoom, how much of a room to draw. Render-only: never writes game state. */
#include "sm64_rooms.h"
#include "game/mario.h"
static sm64_rooms sRooms;
static int sRoomsFile;
static int sRoomsPrevScreen = -1, sRoomsLevel = -1, sRoomsArea = -1, sRoomRevealOn = -1;
static struct { uint32_t timer; int level, area, mario, door, valid; } sRoomsKey;
static int rooms_path(char *buf, size_t cap, int file) { return sm64_rooms_path(buf, cap, sSavePath, file); }
/* The room the room switch (geo_switch_area) finds for Mario now, intangible floors included. Read-only: find_floor's
 * intangible flag and counters are put back. */
static int room_under_mario(void) {
    const struct Object *o = gMarioStates[0].marioObj;
    if (o == NULL) { return -1; }
    const s16 intangible = gFindFloorIncludeSurfaceIntangible;
    const s32 misses = gNumFindFloorMisses;
    const struct NumTimesCalled calls = gNumCalls;
    struct Surface *f = NULL;
    gFindFloorIncludeSurfaceIntangible = TRUE;
    find_floor(o->oPosX, o->oPosY, o->oPosZ, &f);
    gFindFloorIncludeSurfaceIntangible = intangible; gNumFindFloorMisses = misses; gNumCalls = calls;
    return f != NULL ? f->room : -1;
}
/* Right after an area change gMarioCurrentRoom can still be the previous area's. Intangible floors count as in the
 * switch, so BBH's merry-go-round room settles. */
static int mario_room_settled(void) {
    return gMarioCurrentRoom > 0 && room_under_mario() == gMarioCurrentRoom;
}
static int vanilla_room_at(f32 x, f32 y, f32 z) {
    const s16 intangible = gFindFloorIncludeSurfaceIntangible;
    const s32 misses = gNumFindFloorMisses;
    const struct NumTimesCalled calls = gNumCalls;
    struct Surface *f = NULL;
    gFindFloorIncludeSurfaceIntangible = FALSE;
    const f32 h = find_floor(x, y, z, &f);
    int room = f != NULL ? f->room : 0;
    if (f != NULL && room == 0) {
        gFindFloorIncludeSurfaceIntangible = FALSE;
        find_floor(x, h - 100.f, z, &f);
        room = f != NULL ? f->room : 0;
    }
    gFindFloorIncludeSurfaceIntangible = intangible; gNumFindFloorMisses = misses; gNumCalls = calls;
    return room > 0 && room <= 0xFF ? room : 0;
}
#ifdef SM64AR_TEST_SEAMS
static int sTestRoomsSaveFails;
#else
enum { sTestRoomsSaveFails = 0 };
#endif
static void rooms_save_event(s32 src, s32 dst) {
    static sm64_rooms copy;
    char from[1100], to[1100];
    if (!rooms_path(to, sizeof to, dst + 1)) { return; }
    if (src < 0) {
        remove(to);
        if (sRoomsFile == dst + 1) { sm64_rooms_clear(&sRooms); sRoomsKey.valid = 0; }
        printf("AR_ROOMS erased file=%d\n", dst + 1);
        return;
    }
    if (!rooms_path(from, sizeof from, src + 1)) { return; }
    if (sRoomsFile == src + 1 && sRooms.dirty && !sm64_rooms_save(&sRooms, from)) {
        printf("AR_ROOMS copy file=%d: saving its rooms failed (the copy takes the ones on disk)\n", src + 1);
    }
    const int loaded = sm64_rooms_load(&copy, from);
    /* a failed copy leaves the target with no sidecar, never the rooms of the file it replaces */
    const int saved = loaded && !sTestRoomsSaveFails && sm64_rooms_save(&copy, to);
    if (!saved) { remove(to); sm64_rooms_clear(&copy); }
    if (sRoomsFile == dst + 1) { sm64_rooms_load(&sRooms, to); sRoomsKey.valid = 0; }
    if (loaded && !saved) { printf("AR_ROOMS copy file=%d to=%d failed: the target has no rooms\n", src + 1, dst + 1); }
    else { printf("AR_ROOMS copied file=%d to=%d visited=%d\n", src + 1, dst + 1, sm64_rooms_count(&copy)); }
}
static void rooms_tick(void) {
    char path[1100];
    gArSaveFileEvent = rooms_save_event;
    gArObjectRoomFallback = object_room_fallback;
    const int on = sm64_settings_choice("room_reveal") == 0;
    if (on != sRoomRevealOn) {
        sRoomRevealOn = on;
        sm64_rooms_restart_view(&sRooms); sRoomsKey.valid = 0;
    }
    if (gCurrLevelNum != sRoomsLevel || gCurrAreaIndex != sRoomsArea) {
        sRoomsLevel = gCurrLevelNum; sRoomsArea = gCurrAreaIndex;
        sm64_rooms_restart_view(&sRooms); sRoomsKey.valid = 0;
    }
    const int file = gCurrSaveFileNum;
    const int screen = sScreenNow;
    if (sArMode && sm64_host_story() && sRoomsPrevScreen == SM64_SCREEN_FILE_SELECT && screen != SM64_SCREEN_FILE_SELECT &&
        file >= 1 && file <= NUM_SAVE_FILES && !save_file_exists(file - 1)) {
        if (sRoomsFile >= 1 && sRooms.dirty && rooms_path(path, sizeof path, sRoomsFile)) { sm64_rooms_save(&sRooms, path); }
        sm64_rooms_clear(&sRooms);
        if (rooms_path(path, sizeof path, file)) { remove(path); }
        sRoomsFile = file;
        printf("AR_ROOMS fresh save file=%d\n", file);
    } else if (file != sRoomsFile && file >= 1) {
        if (sRoomsFile >= 1 && sRooms.dirty && rooms_path(path, sizeof path, sRoomsFile)) { sm64_rooms_save(&sRooms, path); }
        const int loaded = rooms_path(path, sizeof path, file) && sm64_rooms_load(&sRooms, path);
        sRoomsFile = file;
        printf("AR_ROOMS file=%d loaded=%d visited=%d\n", file, loaded, sm64_rooms_count(&sRooms));
    }
    sRoomsPrevScreen = screen;
    if (mario_room_settled() && screen == SM64_SCREEN_LEVEL && sm64_host_level_ready()) {
        sm64_rooms_visit(&sRooms, gCurrLevelNum, gCurrAreaIndex, gMarioCurrentRoom);
    }
    if (sRoomsFile >= 1 && sRooms.dirty && rooms_path(path, sizeof path, sRoomsFile)) { sm64_rooms_save(&sRooms, path); }
}
/* Objects with no room of their own (oRoom is set only by behaviours that call bhv_init_room) draw in the room
 * bhv_init_room would give them (gArObjectRoomFallback, 0009-Room-tags.patch), so they hide with a hidden room. */
#define OBJ_ROOM_MOVE 100.f
#define OBJ_ROOM_TICKS 30u
static struct { const BehaviorScript *beh; f32 x, y, z; unsigned at; u8 room, valid, active; } sObjRooms[OBJECT_POOL_CAPACITY];
static int sObjRoomsOn;
static int sObjRoomsLevel = -1, sObjRoomsArea = -1;
static unsigned sObjRoomsInits;
static int level_has_rooms(int level) { return level == LEVEL_BBH || level == LEVEL_CASTLE || level == LEVEL_HMC; }
static int own_room(const struct Object *o) {
    const s32 r = o->oFloorRoom > 0 ? o->oFloorRoom : o->oRoom;
    return r > 0 && r <= 0xFF ? (int)r : 0;
}
static int mario_room_tag(void) { return gMarioCurrentRoom > 0 && gMarioCurrentRoom <= 0xFF ? gMarioCurrentRoom : 0; }
static int from_mario(const struct Object *o) {
    const struct Object *m = gMarioStates[0].marioObj;
    for (int k = 0; m != NULL && o != NULL && k < 4; k++) {
        const struct Object *p = o->parentObj;
        if (p == m) { return 1; }
        if (p == NULL || p == o) { return 0; }
        o = p;
    }
    return 0;
}
static void object_rooms_tick(void) {
    const int on = sArMode && gArDrawAllRooms && sRoomRevealOn == 1 && level_has_rooms(gCurrLevelNum) && sm64_host_level_ready();
    if (!on || gCurrLevelNum != sObjRoomsLevel || gCurrAreaIndex != sObjRoomsArea || gArLevelInits != sObjRoomsInits) {
        memset(sObjRooms, 0, sizeof sObjRooms);
        sObjRoomsLevel = gCurrLevelNum; sObjRoomsArea = gCurrAreaIndex; sObjRoomsInits = gArLevelInits;
    }
    sObjRoomsOn = on;
    if (!on) { return; }
    const struct Object *mario = gMarioStates[0].marioObj;
    for (int i = 0; i < OBJECT_POOL_CAPACITY; i++) {
        const struct Object *o = &gObjectPool[i];
        const int active = (o->activeFlags & ACTIVE_FLAG_ACTIVE) != 0;
        const int fresh = active && (!sObjRooms[i].active || !sObjRooms[i].valid || sObjRooms[i].beh != o->behavior);
        sObjRooms[i].active = (u8)active;
        if (!active || o == mario || own_room(o) || from_mario(o)) { sObjRooms[i].valid = 0; continue; }
        const int moved = fabsf(o->oPosX - sObjRooms[i].x) > OBJ_ROOM_MOVE || fabsf(o->oPosY - sObjRooms[i].y) > OBJ_ROOM_MOVE ||
                          fabsf(o->oPosZ - sObjRooms[i].z) > OBJ_ROOM_MOVE;
        if (!fresh && !moved && sHostTicks - sObjRooms[i].at < OBJ_ROOM_TICKS) { continue; }
        sObjRooms[i].room = (u8)vanilla_room_at(o->oPosX, o->oPosY, o->oPosZ);
        sObjRooms[i].beh = o->behavior;
        sObjRooms[i].x = o->oPosX; sObjRooms[i].y = o->oPosY; sObjRooms[i].z = o->oPosZ;
        sObjRooms[i].at = sHostTicks; sObjRooms[i].valid = 1;
    }
}
static int is_door(const struct Object *o) {
    return o->behavior != NULL && (o->behavior == segmented_to_virtual(bhvDoor) || o->behavior == segmented_to_virtual(bhvDoorWarp) ||
                                   o->behavior == segmented_to_virtual(bhvStarDoor));
}
static int door_side(s32 room) { return room > 0 && room <= 0xFF ? (int)room : 0; }
/* gArObjectRoomFallback (the graph walk, game thread). A door draws in whichever of its two rooms is drawn more, as
 * vanilla draws a door from both sides. */
static s32 object_room_fallback(struct Object *o) {
    if (!sObjRoomsOn || o == NULL || o == gMarioStates[0].marioObj) { return 0; }
    if (from_mario(o)) {
        const int room = mario_room_tag();
        if (room > 0) { sm64_host_room_coverage(room); }
        return sm64_rooms_spawn_room(&sRooms, room, gGlobalTimer);
    }
    if (is_door(o)) {
        const int front = door_side(o->oDoorUnkFC), back = door_side(o->oDoorUnk100);
        if (front || back) {
            sm64_host_room_coverage(front ? front : back);
            return sm64_rooms_door_room(&sRooms, front, back, gGlobalTimer);
        }
    }
    for (int k = 0; o != NULL && k < 4; k++) {
        const ptrdiff_t i = o - gObjectPool;
        if (i < 0 || i >= OBJECT_POOL_CAPACITY) { return 0; }
        if (k > 0 && own_room(o)) { return own_room(o); }
        if (sObjRooms[i].valid && sObjRooms[i].beh == o->behavior) { return sObjRooms[i].room; }
        if (o->parentObj == o) { return 0; }
        o = o->parentObj;
    }
    return 0;
}
static int object_room_tag(struct Object *o) {
    s32 room = o->oFloorRoom > 0 ? o->oFloorRoom : o->oRoom;
    if (room <= 0) { room = object_room_fallback(o); }
    return room > 0 && room <= 0xFF ? (int)room : 0;
}
int sm64_host_test_room_at(float x, float y, float z) { return vanilla_room_at(x, y, z); }
int sm64_host_test_spawn_roomless(float x, float y, float z) {
    struct Object *m = gMarioStates[0].marioObj;
    struct Object *c = m ? spawn_object(m, MODEL_YELLOW_COIN, bhvStaticObject) : NULL;
    if (!c) { return -1; }
    c->parentObj = c;
    c->oPosX = c->oHomeX = x; c->oPosY = c->oHomeY = y; c->oPosZ = c->oHomeZ = z;
    return (int)(c - gObjectPool);
}
int sm64_host_test_roomless_objects(float *xyz, int *index, int cap) {
    int n = 0;
    for (int i = 0; i < OBJECT_POOL_CAPACITY; i++) {
        const struct Object *o = &gObjectPool[i];
        if (!(o->activeFlags & ACTIVE_FLAG_ACTIVE) || o == gMarioStates[0].marioObj || own_room(o) || from_mario(o) ||
            !(o->header.gfx.node.flags & GRAPH_RENDER_ACTIVE) || o->header.gfx.sharedChild == NULL) { continue; }
        if (n < cap) { xyz[n * 3] = o->oPosX; xyz[n * 3 + 1] = o->oPosY; xyz[n * 3 + 2] = o->oPosZ; index[n] = i; }
        n++;
    }
    return n;
}
int sm64_host_room_coverage(int room) {
    if (sRoomRevealOn != 1 || !gArDrawAllRooms || room <= 0) { return 255; }
    const int door = (gDoorCurrentRoom > 0 && gDoorCurrentRoom < 60) ? gDoorCurrentRoom : 0;
    if (!sRoomsKey.valid || sRoomsKey.timer != gGlobalTimer || sRoomsKey.level != gCurrLevelNum ||
        sRoomsKey.area != gCurrAreaIndex || sRoomsKey.mario != gMarioCurrentRoom * 2 + mario_room_settled() ||
        sRoomsKey.door != door) {
        const int settled = mario_room_settled();
        const int entering[4] = { door, door ? gDoorAdjacentRooms[door][0] : 0, door ? gDoorAdjacentRooms[door][1] : 0,
                                  settled ? 0 : gMarioCurrentRoom };
        /* Vanilla's gDoorAdjacentRooms is per level, not per area: the castle's floors reuse room numbers. */
        sm64_rooms_clear_doorways(&sRooms);
        for (int i = 0; i < OBJECT_POOL_CAPACITY; i++) {
            const struct Object *o = &gObjectPool[i];
            if ((o->activeFlags & ACTIVE_FLAG_ACTIVE) && is_door(o)) {
                sm64_rooms_doorway(&sRooms, door_side(o->oDoorUnkF8), door_side(o->oDoorUnkFC), door_side(o->oDoorUnk100));
            }
        }
        sm64_rooms_update(&sRooms, gCurrLevelNum, gCurrAreaIndex, settled ? gMarioCurrentRoom : 0, entering, 4, gGlobalTimer);
        sRoomsKey.timer = gGlobalTimer; sRoomsKey.level = gCurrLevelNum; sRoomsKey.area = gCurrAreaIndex;
        sRoomsKey.mario = gMarioCurrentRoom * 2 + settled; sRoomsKey.door = door; sRoomsKey.valid = 1;
    }
    return sm64_rooms_coverage(&sRooms, room, gGlobalTimer);
}
int sm64_host_rooms_visited(int level, int area, int room) { return sm64_rooms_visited(&sRooms, level, area, room); }
int sm64_host_rooms_count(void) { return sm64_rooms_count(&sRooms); }
int sm64_host_rooms_file(void) { return sRoomsFile; }
int sm64_host_rooms_path(char *buf, size_t cap) { return sRoomsFile >= 1 && rooms_path(buf, cap, sRoomsFile); }
int sm64_host_door_room(void) { return gDoorCurrentRoom; }
static struct Object *pool_object(int i) {
    return (i >= 0 && i < OBJECT_POOL_CAPACITY && (gObjectPool[i].activeFlags & ACTIVE_FLAG_ACTIVE)) ? &gObjectPool[i] : NULL;
}
int sm64_host_test_spawn_coin(float x, float y, float z) {
    struct Object *m = gMarioStates[0].marioObj;
    struct Object *c = m ? spawn_object(m, MODEL_YELLOW_COIN, bhvSingleCoinGetsSpawned) : NULL;
    if (!c) { return -1; }
    c->oPosX = x; c->oPosY = y; c->oPosZ = z;
    return (int)(c - gObjectPool);
}
int sm64_host_test_move_object(int index, float x, float y, float z) {
    struct Object *o = pool_object(index);
    if (!o) { return 0; }
    o->oPosX = o->oHomeX = x; o->oPosY = o->oHomeY = y; o->oPosZ = o->oHomeZ = z;
    return 1;
}
#ifdef SM64AR_TEST_SEAMS
void sm64_host_test_rooms_save_fails(int on) { sTestRoomsSaveFails = on != 0; }
void sm64_host_test_stale_room(int room) {
    gRoomOverride = (s16)room;
    if (room >= 0) { gMarioCurrentRoom = (s16)room; gMarioStates[0].currentRoom = (s16)room; }
}
void sm64_host_test_mario_render_active(int on) {
    struct Object *o = gMarioStates[0].marioObj;
    if (!o) { return; }
    if (on) { o->header.gfx.node.flags |= GRAPH_RENDER_ACTIVE; } else { o->header.gfx.node.flags &= ~GRAPH_RENDER_ACTIVE; }
}
#endif
int sm64_host_test_floors_of_type(int type, float *xyz, int *room, int cap) {
    struct Surface **s; const int n = collect_static(SPATIAL_PARTITION_FLOORS, &s);
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (s[i]->type != type) { continue; }
        if (k < cap) {
            xyz[k * 3 + 0] = (s[i]->vertex1[0] + s[i]->vertex2[0] + s[i]->vertex3[0]) / 3.f;
            xyz[k * 3 + 1] = (s[i]->vertex1[1] + s[i]->vertex2[1] + s[i]->vertex3[1]) / 3.f;
            xyz[k * 3 + 2] = (s[i]->vertex1[2] + s[i]->vertex2[2] + s[i]->vertex3[2]) / 3.f;
            room[k] = s[i]->room;
        }
        k++;
    }
    return k;
}
int sm64_host_test_room_under_mario(void) { return room_under_mario(); }
int sm64_host_test_object_floor_room(int index) { struct Object *o = pool_object(index); return o ? o->oFloorRoom : -999; }
extern struct SaveBuffer gSaveBuffer;
size_t sm64_host_test_save_buffer(void *buf, size_t cap) {
    if (buf && cap >= sizeof gSaveBuffer) { memcpy(buf, &gSaveBuffer, sizeof gSaveBuffer); }
    return sizeof gSaveBuffer;
}
void sm64_host_test_save_copy(int src_file, int dst_file) { save_file_copy(src_file - 1, dst_file - 1); }
void sm64_host_test_save_erase(int file) { save_file_erase(file - 1); }
void sm64_host_test_save_write(void) { save_file_do_save(gCurrSaveFileNum - 1, TRUE); }
int sm64_host_test_walk_forward(float speed) {
    struct MarioState *m = &gMarioStates[0];
    if (!m->marioObj) { return 0; }
    m->forwardVel = speed;
    set_mario_action(m, ACT_WALKING, 0);
    return 1;
}
int sm64_host_test_room_objects(sm64_room_object *out, int cap) {
    const s16 mario = gMarioCurrentRoom;
    int n = 0;
    for (int i = 0; i < OBJECT_POOL_CAPACITY; i++) {
        struct Object *o = &gObjectPool[i];
        if (!(o->activeFlags & ACTIVE_FLAG_ACTIVE) || o->oRoom == -1) { continue; }
        if (n < cap) {
            sm64_room_object *r = &out[n];
            for (int k = 0; k < 3; k++) { r->pos[k] = (&o->oPosX)[k]; }
            r->room = o->oRoom; r->action = o->oAction; r->anim_frame = o->header.gfx.animInfo.animFrame;
            r->other = mario > 0 && o->oRoom != mario && gDoorAdjacentRooms[mario][0] != o->oRoom &&
                       gDoorAdjacentRooms[mario][1] != o->oRoom;
            r->flagged = (o->activeFlags & ACTIVE_FLAG_IN_DIFFERENT_ROOM) != 0;
            r->drawn = (o->header.gfx.node.flags & GRAPH_RENDER_ACTIVE) != 0;
            r->behavior = (int32_t)get_id_from_behavior(o->behavior);
        }
        n++;
    }
    return n;
}
int sm64_host_test_doors(sm64_door_info *out, int cap) {
    int n = 0;
    for (int i = 0; i < OBJECT_POOL_CAPACITY; i++) {
        struct Object *o = &gObjectPool[i];
        if (!(o->activeFlags & ACTIVE_FLAG_ACTIVE)) { continue; }
        const int id = (int)get_id_from_behavior(o->behavior);
        if (id != id_bhvDoor && id != id_bhvDoorWarp && id != id_bhvStarDoor) { continue; }
        if (n < cap) {
            sm64_door_info *d = &out[n];
            for (int k = 0; k < 3; k++) { d->pos[k] = (&o->oPosX)[k]; }
            d->behavior = id; d->action = o->oAction;
            d->doorway = o->oDoorUnkF8; d->front = o->oDoorUnkFC; d->back = o->oDoorUnk100;
            d->tag = object_room_tag(o);
            d->drawn = (o->header.gfx.node.flags & GRAPH_RENDER_ACTIVE) != 0;
            d->index = i; d->stars = (int32_t)((u32)o->oBehParams >> 24);
        }
        n++;
    }
    return n;
}
int sm64_host_test_spawn_from_mario(float x, float y, float z) {
    struct Object *m = gMarioStates[0].marioObj;
    struct Object *c = m ? spawn_object(m, MODEL_YELLOW_COIN, bhvStaticObject) : NULL;
    if (!c) { return -1; }
    c->oPosX = c->oHomeX = x; c->oPosY = c->oHomeY = y; c->oPosZ = c->oHomeZ = z;
    return (int)(c - gObjectPool);
}
int sm64_host_test_object_invisible(int index, int on) {
    struct Object *o = pool_object(index);
    if (!o) { return 0; }
    if (on) { o->header.gfx.node.flags |= GRAPH_RENDER_INVISIBLE; } else { o->header.gfx.node.flags &= ~GRAPH_RENDER_INVISIBLE; }
    return 1;
}
int sm64_host_test_face_door(int index, float dist, int side) {
    struct MarioState *m = &gMarioStates[0];
    struct Object *o = pool_object(index);
    if (!m->marioObj || !o) { return 0; }
    const float s = side >= 0 ? 1.f : -1.f;
    m->pos[0] = o->oPosX + s * sins(o->oMoveAngleYaw) * dist; m->pos[2] = o->oPosZ + s * coss(o->oMoveAngleYaw) * dist;
    struct Surface *floor = NULL;
    m->pos[1] = find_floor(m->pos[0], o->oPosY + 100.f, m->pos[2], &floor);
    m->faceAngle[1] = atan2s(o->oPosZ - m->pos[2], o->oPosX - m->pos[0]);
    m->forwardVel = 0.f;
    set_mario_action(m, ACT_IDLE, 0);
    return 1;
}
void sm64_host_test_hurt(int wedges) { gMarioStates[0].hurtCounter += (u8)(4 * wedges); }
void sm64_host_test_heal(int wedges) { gMarioStates[0].healCounter += (u8)(4 * wedges); }
void sm64_host_test_add_coins(int n) { gMarioStates[0].numCoins += (s16)n; }
int  sm64_host_paused(void) { return sCurrPlayMode == PLAY_MODE_PAUSED; }
#include "game/interaction.h"
#include "game/mario.h"
int sm64_host_test_enter_painting(int index) {
    struct Surface **s; const int n = collect_static(SPATIAL_PARTITION_FLOORS, &s);
    int found = 0;
    for (int i = 0; i < n && !found; i++) {
        if (s[i]->type != SURFACE_PAINTING_WARP_D3 + index) continue;
        struct MarioState *m = &gMarioStates[0];
        m->pos[0] = (s[i]->vertex1[0] + s[i]->vertex2[0] + s[i]->vertex3[0]) / 3.f;
        m->pos[1] = (s[i]->vertex1[1] + s[i]->vertex2[1] + s[i]->vertex3[1]) / 3.f + 10.f;
        m->pos[2] = (s[i]->vertex1[2] + s[i]->vertex2[2] + s[i]->vertex3[2]) / 3.f;
        m->vel[0] = m->vel[1] = m->vel[2] = 0.f; m->forwardVel = 0.f;
        set_mario_action(m, ACT_FREEFALL, 0);
        found = 1;
    }
    free(s);
    return found;
}

int sm64_host_test_enter_cannon(void) {
    struct MarioState *m = &gMarioStates[0];
    const int cannon = sm64_host_behavior_id("bhvCannon");
    struct Object *best = NULL;
    float bestd = 1e30f;
    for (int i = 0; i < OBJECT_POOL_CAPACITY; i++) {
        struct Object *o = &gObjectPool[i];
        if (!(o->activeFlags & ACTIVE_FLAG_ACTIVE) || (int)get_id_from_behavior(o->behavior) != cannon || o->oAction != 0) { continue; }
        const float dx = o->oPosX - m->pos[0], dz = o->oPosZ - m->pos[2], d = dx * dx + dz * dz;
        if (d < bestd) { bestd = d; best = o; }
    }
    if (!best || !m->marioObj) { return 0; }
    m->pos[0] = best->oPosX; m->pos[1] = best->oPosY; m->pos[2] = best->oPosZ;
    best->oInteractStatus = INT_STATUS_INTERACTED;
    best->oCannonPlayerIndex = 0;
    m->interactObj = best; m->usedObj = best;
    set_mario_action(m, ACT_IN_CANNON, 0);
    return 1;
}
int sm64_host_mario_cannon_aiming(void) { return gMarioStates[0].action == ACT_IN_CANNON && gMarioStates[0].actionState == 2; }
int sm64_host_mario_shot_from_cannon(void) { return gMarioStates[0].action == ACT_SHOT_FROM_CANNON; }
#include "sm64_cannon.h"
#include "engine/math_util.h"
int sm64_host_star_pos(int star_index, float out[3]) {
    const int star = sm64_host_behavior_id("bhvStar");
    for (int i = 0; i < OBJECT_POOL_CAPACITY; i++) {
        struct Object *o = &gObjectPool[i];
        if (!(o->activeFlags & ACTIVE_FLAG_ACTIVE) || (int)get_id_from_behavior(o->behavior) != star) { continue; }
        if ((int)((o->oBehParams >> 24) & 0xFF) != star_index) { continue; }
        out[0] = o->oPosX; out[1] = o->oPosY; out[2] = o->oPosZ;
        return 1;
    }
    return 0;
}
int sm64_host_mario_in_cannon(void) { return gMarioStates[0].action == ACT_IN_CANNON; }
int sm64_host_cannon_angles(int *pitch, int *yaw) {
    const struct MarioState *m = &gMarioStates[0];
    if (m->action != ACT_IN_CANNON || m->actionState != 2) { return 0; }
    *pitch = m->faceAngle[0]; *yaw = m->faceAngle[1];
    return 1;
}
int sm64_host_cannon_yaw_limits(int *lo, int *hi) {
    const struct MarioState *m = &gMarioStates[0];
    if (m->action != ACT_IN_CANNON || !m->marioObj) { return 0; }
    const int base = (s16)m->marioObj->oMarioCannonObjectYaw;
    *lo = base - 0x4000; *hi = base + 0x4000;
    return 1;
}
uint32_t sm64_host_cannon_arc(int pitch, int yaw, float *xyz, uint32_t cap) {
    const struct MarioState *m = &gMarioStates[0];
    return sm64_cannon_arc(m->pos, sins((s16)pitch), coss((s16)pitch), sins((s16)yaw), coss((s16)yaw), sm64_ar_floor_at, NULL,
                           xyz, cap, SM64_CANNON_ARC_MAX_TICKS);
}
static struct Object *cannon_barrel_of(const struct Object *base) {
    const int barrel = sm64_host_behavior_id("bhvCannonBarrel");
    for (int i = 0; i < OBJECT_POOL_CAPACITY; i++) {
        struct Object *o = &gObjectPool[i];
        if ((o->activeFlags & ACTIVE_FLAG_ACTIVE) && (int)get_id_from_behavior(o->behavior) == barrel && o->parentObj == base) { return o; }
    }
    return NULL;
}
int sm64_host_cannon_rendered(int *base, int *barrel) {
    const struct MarioState *m = &gMarioStates[0];
    if (m->action != ACT_IN_CANNON || !m->usedObj) { return 0; }
    const struct Object *b = cannon_barrel_of(m->usedObj);
    *base = (m->usedObj->header.gfx.node.flags & GRAPH_RENDER_ACTIVE) ? 1 : 0;
    *barrel = b && (b->header.gfx.node.flags & GRAPH_RENDER_ACTIVE) ? 1 : 0;
    return 1;
}
int sm64_host_cannon_barrel_axes(float axes[9]) {
    const struct MarioState *m = &gMarioStates[0];
    if (m->action != ACT_IN_CANNON || !m->usedObj) { return 0; }
    struct Object *b = cannon_barrel_of(m->usedObj);
    if (!b) { return 0; }
    Mat4 mt;
    Vec3f zero = { 0.f, 0.f, 0.f };
    mtxf_rotate_zxy_and_translate(mt, zero, b->header.gfx.angle);
    for (int r = 0; r < 3; r++) { for (int k = 0; k < 3; k++) { axes[r * 3 + k] = mt[r][k]; } }
    return 1;
}
int sm64_host_mario_on_ground(void) { return !(gMarioStates[0].action & ACT_FLAG_AIR); }
int sm64_host_mario_holding(void) { return gMarioStates[0].heldObj != NULL; }
const char *sm64_host_behavior_name(int id) {
    const char *n = (id >= 0 && id < (int)id_bhv_max_count) ? get_behavior_name_from_id((enum BehaviorId)id) : NULL;
    return n ? n : "";
}
int sm64_host_nearest_object(int behavior_id, const float from[3], float max_dy, float out[3]) {
    float best = 1e30f; int found = 0;
    for (int i = 0; i < OBJECT_POOL_CAPACITY; i++) {
        struct Object *o = &gObjectPool[i];
        if (!(o->activeFlags & ACTIVE_FLAG_ACTIVE) || (int)get_id_from_behavior(o->behavior) != behavior_id) { continue; }
        if (fabsf(o->oPosY - from[1]) > max_dy) { continue; }
        const float dx = o->oPosX - from[0], dz = o->oPosZ - from[2], d = dx * dx + dz * dz;
        if (d < best) { best = d; out[0] = o->oPosX; out[1] = o->oPosY; out[2] = o->oPosZ; found = 1; }
    }
    return found;
}
int sm64_host_nearest_open_cannon(const float from[3], float out[3]) {
    const int cannon = sm64_host_behavior_id("bhvCannon");
    float best = 1e30f; int found = 0;
    for (int i = 0; i < OBJECT_POOL_CAPACITY; i++) {
        struct Object *o = &gObjectPool[i];
        if (!(o->activeFlags & ACTIVE_FLAG_ACTIVE) || (int)get_id_from_behavior(o->behavior) != cannon || o->oAction != 0) { continue; }
        const float dx = o->oPosX - from[0], dz = o->oPosZ - from[2], d = dx * dx + dz * dz;
        if (d < best) { best = d; out[0] = o->oPosX; out[1] = o->oPosY; out[2] = o->oPosZ; found = 1; }
    }
    return found;
}
int sm64_host_test_place_mario(float x, float y, float z) {
    struct MarioState *m = &gMarioStates[0];
    struct Surface *floor = NULL;
    if (!m->marioObj) { return 0; }
    const float fy = find_floor(x, y + 100.f, z, &floor);
    if (!floor) { return 0; }
    m->pos[0] = x; m->pos[1] = fy; m->pos[2] = z;
    m->vel[0] = m->vel[1] = m->vel[2] = 0.f;
    m->forwardVel = 0.f;
    set_mario_action(m, ACT_IDLE, 0);
    return 1;
}
int sm64_host_test_drop_mario(float x, float y, float z) {
    struct MarioState *m = &gMarioStates[0];
    if (!m->marioObj) { return 0; }
    m->pos[0] = x; m->pos[1] = y; m->pos[2] = z;
    m->vel[0] = m->vel[1] = m->vel[2] = 0.f;
    m->forwardVel = 0.f;
    set_mario_action(m, ACT_FREEFALL, 0);
    return 1;
}
int sm64_host_test_face_object(int behavior_id, float dist) {
    struct MarioState *m = &gMarioStates[0];
    float o[3];
    if (!m->marioObj || !sm64_host_nearest_object(behavior_id, m->pos, 1e9f, o)) { return 0; }
    const float dx = m->pos[0] - o[0], dz = m->pos[2] - o[2], len = sqrtf(dx * dx + dz * dz);
    const float ux = len > 1.f ? dx / len : 0.f, uz = len > 1.f ? dz / len : 1.f;
    m->pos[0] = o[0] + ux * dist; m->pos[2] = o[2] + uz * dist;
    struct Surface *floor = NULL;
    m->pos[1] = find_floor(m->pos[0], o[1] + 100.f, m->pos[2], &floor);
    m->faceAngle[1] = atan2s(o[2] - m->pos[2], o[0] - m->pos[0]);   /* SM64's own atan2s (an s16 table), not atan2f */
    m->forwardVel = 0.f;
    set_mario_action(m, ACT_IDLE, 0);
    return 1;
}
#include "behavior_data.h"
#include "model_ids.h"
#include "course_table.h"
#include "game/object_helpers.h"
#include "game/save_file.h"
static struct Object *spawn_test_star(int star_index) {
    struct MarioState *m = &gMarioStates[0];
    if (!m->marioObj) { return NULL; }
    struct Object *s = spawn_object_abs_with_rot(m->marioObj, 0, MODEL_STAR, bhvStar, (s16)m->pos[0], (s16)(m->pos[1] + 40.f),
                                                 (s16)m->pos[2], 0, 0, 0);
    if (s) { s->oBehParams = (u32)star_index << 24; }   /* read by bhv_collect_star_init on the star's first update */
    return s;
}
int sm64_host_test_spawn_star(int star_index) { return spawn_test_star(star_index) != NULL; }
int sm64_host_saves(int *course, int *star) {
    if (course) { *course = gArSaveCourse; }
    if (star) { *star = gArSaveStar; }
    return (int)gArSaveCount;
}
int sm64_host_menu_mode(void) { return gMenuMode; }
int sm64_host_time_stop(void) { return (int)gTimeStopState; }
int sm64_host_save_prompt_open(void) {
    const int d = get_dialog_id();
    return gMenuMode != -1 || d == gBehaviorValues.dialogs.CollectedStarDialog || d == gBehaviorValues.dialogs.HundredCoinsDialog;
}
int sm64_host_save_unwritten(void) { return gSaveFileModified != 0; }
int sm64_host_test_spawn_star_noexit(int star_index) {
    struct Object *s = spawn_test_star(star_index);
    if (s) { s->oInteractionSubtype |= INT_SUBTYPE_NO_EXIT; }
    return s != NULL;
}
int sm64_host_test_spawn_bowser_key(void) {
    struct MarioState *m = &gMarioStates[0];
    if (!m->marioObj) { return 0; }
    return spawn_object_abs_with_rot(m->marioObj, 0, MODEL_BOWSER_KEY, bhvBowserKey, (s16)m->pos[0], (s16)(m->pos[1] + 40.f),
                                     (s16)m->pos[2], 0, 0, 0) != NULL;
}
void sm64_host_test_grand_star_warp(void) { level_trigger_warp(&gMarioStates[0], WARP_OP_CREDITS_START); }
int sm64_host_test_grand_star_ending(void) {
    if (!sm64_host_story() || gMarioStates[0].marioObj == NULL) { return 0; }
    level_trigger_warp(&gMarioStates[0], WARP_OP_CREDITS_START);
    return 1;
}
int sm64_host_test_open_sign(int id) {
    if (get_dialog_id() != DIALOG_NONE) { return 0; }
    create_dialog_inverted_box((s16)id);
    return 1;
}
int sm64_host_test_open_dialog(int id) {
    if (get_dialog_id() != DIALOG_NONE) { return 0; }
    create_dialog_box((s16)id);
    return 1;
}
void sm64_host_test_kill_mario(void) { gMarioStates[0].health = 0xFF; gMarioStates[0].hurtCounter = 0; gMarioStates[0].healCounter = 0; }
uint32_t sm64_host_bob_star_flags(void) { return save_file_get_star_flags(gCurrSaveFileNum - 1, COURSE_BOB - 1); }
void sm64_host_test_save_bob_star(int star_index) { save_file_set_star_flags(gCurrSaveFileNum - 1, COURSE_BOB - 1, 1u << star_index); }
uint32_t sm64_host_course_star_flags(int course) {
    return course >= COURSE_MIN && course < COURSE_COUNT ? save_file_get_star_flags(gCurrSaveFileNum - 1, course - 1) : 0;
}
uint32_t sm64_host_save_star_flags(int slot) {
    if (slot == 0) { return save_file_get_star_flags(gCurrSaveFileNum - 1, -1); }
    return sm64_host_course_star_flags(slot);
}
void sm64_host_seed_course_stars(int course, uint32_t mask) {
    for (int i = 0; i < 7; i++) {
        if (mask & (1u << i)) { save_file_set_star_flags(gCurrSaveFileNum - 1, course - 1, 1u << i); }
    }
}
void sm64_host_test_save_course_star(int course, int star_index) { save_file_set_star_flags(gCurrSaveFileNum - 1, course - 1, 1u << star_index); }
void sm64_host_seed_bob_stars(uint32_t mask) { sm64_host_seed_course_stars(COURSE_BOB, mask); }
void sm64_host_set_lives(int n) { gMarioStates[0].numLives = (s8)n; }
int sm64_host_in_bob(void) { return gCurrLevelNum == LEVEL_BOB; }
int sm64_host_mario_lives(void) { return gMarioStates[0].numLives; }
int sm64_host_mario_coins(void) { return gMarioStates[0].numCoins; }
int sm64_host_mario_health(void) { return gMarioStates[0].health; }
void sm64_host_test_exit_course(void) { level_trigger_warp(&gMarioStates[0], WARP_OP_EXIT); }
extern s8 gDialogLineNum;
int sm64_host_pause_line(void) { return gDialogLineNum; }
int sm64_host_changing_level(void) { return sCurrPlayMode == PLAY_MODE_CHANGE_LEVEL; }

void sm64_host_test_play_sound(uint32_t bits, int where) {
    static Vec3f sOrigin = { 0.f, 0.f, 0.f };   /* never written: distance 0, pan exactly 0.5 (get_sound_pan's x == z == 0) */
    f32 *pos = where == SM64_SOUND_AT_MARIO && gMarioStates[0].marioObj ? gMarioStates[0].marioObj->header.gfx.cameraToObject
             : where == SM64_SOUND_AT_ORIGIN                            ? sOrigin
                                                                        : gGlobalSoundSource;
    play_sound((s32)bits, pos);
}
void sm64_host_test_play_sound_at(uint32_t bits, int slot, const float offset[3]) {
    static Vec3f sAt[4];
    if (slot < 0 || slot >= 4) { return; }
    sAt[slot][0] = offset[0]; sAt[slot][1] = offset[1]; sAt[slot][2] = offset[2];
    play_sound((s32)bits, sAt[slot]);
}
void sm64_host_test_sound_banks(uint32_t enabled) {
    sound_banks_disable(SEQ_PLAYER_SFX, (u16)(SOUND_BANKS_ALL & ~enabled));
    sound_banks_enable(SEQ_PLAYER_SFX, (u16)(SOUND_BANKS_ALL & enabled));
}
int sm64_host_test_play_secondary_music(int seq) {
    if (seq == 0) { stop_secondary_music(50); return 1; }
    if (seq < 0 || seq >= SEQ_COUNT) { return 0; }
    play_secondary_music((u8)seq, 0, 255, 1000);
    return 1;
}
void sm64_host_test_play_star_jingle(void) { play_power_star_jingle(TRUE); }
