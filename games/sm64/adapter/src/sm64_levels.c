#include "sm64_levels.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define COURSE_ENTRY(key, name, lv, c) { key, name, lv, c, c, 7, 0, 0, 0 }
static const sm64_level_entry sLevels[SM64_LEVEL_COUNT] = {
    { "grounds", "Castle Grounds", SM64_LV_GROUNDS, 0, -1, 0, 0, 1, 0 },
    COURSE_ENTRY("bob", "Bob-omb Battlefield", SM64_LV_BOB, 1),
    COURSE_ENTRY("wf", "Whomp's Fortress", SM64_LV_WF, 2),
    COURSE_ENTRY("jrb", "Jolly Roger Bay", SM64_LV_JRB, 3),
    COURSE_ENTRY("ccm", "Cool, Cool Mountain", SM64_LV_CCM, 4),
    COURSE_ENTRY("bbh", "Big Boo's Haunt", SM64_LV_BBH, 5),
    COURSE_ENTRY("hmc", "Hazy Maze Cave", SM64_LV_HMC, 6),
    COURSE_ENTRY("lll", "Lethal Lava Land", SM64_LV_LLL, 7),
    COURSE_ENTRY("ssl", "Shifting Sand Land", SM64_LV_SSL, 8),
    COURSE_ENTRY("ddd", "Dire, Dire Docks", SM64_LV_DDD, 9),
    COURSE_ENTRY("sl", "Snowman's Land", SM64_LV_SL, 10),
    COURSE_ENTRY("wdw", "Wet-Dry World", SM64_LV_WDW, 11),
    COURSE_ENTRY("ttm", "Tall, Tall Mountain", SM64_LV_TTM, 12),
    COURSE_ENTRY("thi", "Tiny-Huge Island", SM64_LV_THI, 13),
    COURSE_ENTRY("ttc", "Tick Tock Clock", SM64_LV_TTC, 14),
    COURSE_ENTRY("rr", "Rainbow Ride", SM64_LV_RR, 15),
    { "pss", "Princess's Secret Slide", SM64_LV_PSS, 0, 19, 2, 1, 0, 0 },
    { "totwc", "Tower of the Wing Cap", SM64_LV_TOTWC, 0, 21, 1, 1, 0, 0 },
    { "cotmc", "Cavern of the Metal Cap", SM64_LV_COTMC, 0, 20, 1, 1, 0, 0 },
    { "vcutm", "Vanish Cap under the Moat", SM64_LV_VCUTM, 0, 22, 1, 1, 0, 0 },
    { "wmotr", "Wing Mario over the Rainbow", SM64_LV_WMOTR, 0, 23, 1, 1, 0, 0 },
    { "sa", "Secret Aquarium", SM64_LV_SA, 0, 24, 1, 1, 0, 0 },
    { "courtyard", "Castle Courtyard", SM64_LV_COURTYARD, 0, -1, 0, 1, 1, 0 },
    { "castle", "Castle (inside)", SM64_LV_CASTLE, 0, 0, 5, 1, 1, 0 },
    { "bitdw", "Bowser in the Dark World", SM64_LV_BITDW, 0, 16, 1, 1, 0, SM64_LV_BOWSER_1 },
    { "bitfs", "Bowser in the Fire Sea", SM64_LV_BITFS, 0, 17, 1, 1, 0, SM64_LV_BOWSER_2 },
    { "bits", "Bowser in the Sky", SM64_LV_BITS, 0, 18, 1, 1, 0, SM64_LV_BOWSER_3 },
    { "bowserdw", "Bowser 1 (Dark World fight)", SM64_LV_BOWSER_1, 0, -1, 0, 1, 0, 0 },
    { "bowserfs", "Bowser 2 (Fire Sea fight)", SM64_LV_BOWSER_2, 0, -1, 0, 1, 0, 0 },
    { "bowsersky", "Bowser 3 (Sky fight)", SM64_LV_BOWSER_3, 0, -1, 0, 1, 0, 0 },
};
const sm64_level_entry *sm64_level_at(int i) { return i >= 0 && i < SM64_LEVEL_COUNT ? &sLevels[i] : NULL; }
int sm64_level_find(const char *key) {
    if (!key) return -1;
    for (int i = 0; i < SM64_LEVEL_COUNT; i++) if (strcmp(sLevels[i].key, key) == 0) return i;
    return -1;
}
int sm64_level_index_of(int level_num) {
    for (int i = 0; i < SM64_LEVEL_COUNT; i++) if (sLevels[i].level_num == level_num) return i;
    return -1;
}
float sm64_mario_cm_clamp(float cm) {
    if (isnan(cm)) return SM64_MARIO_CM_DEFAULT;
    return cm < SM64_MARIO_CM_MIN ? SM64_MARIO_CM_MIN : cm > SM64_MARIO_CM_MAX ? SM64_MARIO_CM_MAX : cm;
}
float sm64_units_per_meter_for_mario_cm(float cm) { return SM64_MARIO_HEIGHT_UNITS * 100.f / sm64_mario_cm_clamp(cm); }
static void one_decimal(char *buf, size_t cap, float v) {
    snprintf(buf, cap, "%.1f", (double)v);
    const size_t n = strlen(buf);
    if (n >= 2 && strcmp(buf + n - 2, ".0") == 0) buf[n - 2] = 0;
}
void sm64_scale_readout(char *buf, int cap, float mario_cm, float extent_units) {
    if (!buf || cap <= 0) return;
    const float cm = sm64_mario_cm_clamp(mario_cm);
    char m[16], l[24] = "";
    one_decimal(m, sizeof m, cm);
    const float ext_m = extent_units / sm64_units_per_meter_for_mario_cm(cm);
    if (isfinite(ext_m) && ext_m > 0.f) {
        char v[16];
        if (ext_m < 0.995f) snprintf(l, sizeof l, " \xc2\xb7 level ~%d cm", (int)lrintf(ext_m * 100.f));
        else if (ext_m < 9.95f) { one_decimal(v, sizeof v, ext_m); snprintf(l, sizeof l, " \xc2\xb7 level ~%s m", v); }
        else snprintf(l, sizeof l, " \xc2\xb7 level ~%d m", (int)lrintf(ext_m));
    }
    snprintf(buf, (size_t)cap, "Mario %s cm%s", m, l);
}

int sm64_level_anchor_rule(int level_num, int area) {
    switch (level_num) {
    case SM64_LV_CASTLE: case SM64_LV_BBH: return SM64_ANCHOR_ENTRY;
    case SM64_LV_TTM: return area == 1 ? SM64_ANCHOR_LOWEST_FLOOR : SM64_ANCHOR_BASE;
    case SM64_LV_BITS: case SM64_LV_WMOTR: case SM64_LV_COTMC: return area == 1 ? SM64_ANCHOR_LOWEST_FLOOR : SM64_ANCHOR_BASE;
    case SM64_LV_TOTWC: return area == 1 ? SM64_ANCHOR_BOTTOM : SM64_ANCHOR_BASE;
    default: return SM64_ANCHOR_BASE;
    }
}

int sm64_level_star_count(uint32_t flags) {
    int n = 0;
    for (int b = 0; b < 7; b++) n += (int)((flags >> b) & 1u);
    return n;
}
void sm64_level_label(char *buf, int cap, const char *name, uint32_t flags, int star_max, int here, int home) {
    if (!buf || cap <= 0) return;
    snprintf(buf, (size_t)cap, "%s", name ? name : "");
    size_t n = strlen(buf);
    if (star_max > 0 && n < (size_t)cap) snprintf(buf + n, (size_t)cap - n, "  \xE2\x98\x85%d/%d", sm64_level_star_count(flags), star_max);
    n = strlen(buf);
    if (n < (size_t)cap) snprintf(buf + n, (size_t)cap - n, "%s", here ? "  \xC2\xB7 here" : home ? "  \xC2\xB7 picked" : "");
}
void sm64_level_current_name(const sm64_level_state *s, char *buf, int cap) {
    if (!buf || cap <= 0) return;
    const sm64_level_entry *e = s ? sm64_level_at(s->index) : NULL;
    if (e) snprintf(buf, (size_t)cap, "%s", e->name);
    else snprintf(buf, (size_t)cap, "level %d", s ? s->level_num : 0);
}
