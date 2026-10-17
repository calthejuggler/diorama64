/* sm64_host.c asserts the numbers against include/level_table.h and levels/course_defines.h. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SM64_LV_BBH 4
#define SM64_LV_CCM 5
#define SM64_LV_HMC 7
#define SM64_LV_SSL 8
#define SM64_LV_BOB 9
#define SM64_LV_SL 10
#define SM64_LV_WDW 11
#define SM64_LV_JRB 12
#define SM64_LV_THI 13
#define SM64_LV_TTC 14
#define SM64_LV_RR 15
#define SM64_LV_GROUNDS 16
#define SM64_LV_LLL 22
#define SM64_LV_DDD 23
#define SM64_LV_WF 24
#define SM64_LV_TTM 36
#define SM64_LV_CASTLE 6
#define SM64_LV_BITDW 17
#define SM64_LV_VCUTM 18
#define SM64_LV_BITFS 19
#define SM64_LV_SA 20
#define SM64_LV_BITS 21
#define SM64_LV_COURTYARD 26
#define SM64_LV_PSS 27
#define SM64_LV_COTMC 28
#define SM64_LV_TOTWC 29
#define SM64_LV_BOWSER_1 30
#define SM64_LV_WMOTR 31
#define SM64_LV_BOWSER_2 33
#define SM64_LV_BOWSER_3 34
#define SM64_LEVEL_COUNT 30
#define SM64_LEVEL_COURSE_COUNT 16
#define SM64_LEVEL_BOB_INDEX 1
typedef struct sm64_level_entry {
    const char *key; const char *name; int level_num; int course;
    int star_slot, star_max, section, hub, boss_level;
} sm64_level_entry;
const sm64_level_entry *sm64_level_at(int index);
int sm64_level_find(const char *key);
int sm64_level_index_of(int level_num);
enum { SM64_ANCHOR_BASE = 0, SM64_ANCHOR_ENTRY, SM64_ANCHOR_LOWEST_FLOOR, SM64_ANCHOR_BOTTOM };
int sm64_level_anchor_rule(int level_num, int area);
typedef struct sm64_level_state {
    int level_num, index, area, act, home_index, pending;
    uint32_t stars[SM64_LEVEL_COUNT];
    int wait_ticks;
    char wait_reason[40];
    int forced_count;
    char forced_reason[40];
    int save_count, save_course, save_star;
    float extent_units;
    int story;
    int screen;
} sm64_level_state;
#define SM64_AR_UNITS_PER_METER 8000.f
/* Measured on Mario's standing hitbox (mario.c: hitboxHeight 160): Mario mario_cm centimetres tall is 16000 / mario_cm
 * units per metre, one scale for every level. */
#define SM64_MARIO_HEIGHT_UNITS 160.f
#define SM64_MARIO_CM_MIN 0.5f
#define SM64_MARIO_CM_MAX 10.f
#define SM64_MARIO_CM_DEFAULT 2.f
float sm64_mario_cm_clamp(float mario_cm);
float sm64_units_per_meter_for_mario_cm(float mario_cm);
void sm64_scale_readout(char *buf, int cap, float mario_cm, float extent_units);
/* Star flags: bits 0-5 = acts 1-6, bit 6 = the 100-coin star; higher bits (the cannon) are not stars. */
int sm64_level_star_count(uint32_t star_flags);
void sm64_level_label(char *buf, int cap, const char *name, uint32_t star_flags, int star_max, int here, int home);
void sm64_level_current_name(const sm64_level_state *s, char *buf, int cap);
#ifdef __cplusplus
}
#endif
