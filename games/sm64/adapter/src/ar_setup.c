#include "sm64_ar_setup.h"
#include "sm64_levels.h"
#include "sm64_host.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int bad(char *err, size_t cap, const char *what, const char *item) { if (err && cap) snprintf(err, cap, "%s '%s'", what, item); return 0; }
int sm64_setup_parse(const char *text, sm64_setup *out, char *err, size_t cap) {
    sm64_setup s;
    memset(&s, 0, sizeof s);
    if (err && cap) err[0] = 0;
    if (!text) text = "";
    char buf[128];
    if (strlen(text) >= sizeof buf) return bad(err, cap, "setup string too long", "");
    strcpy(buf, text);
    if (!buf[0]) { *out = s; return 1; }
    int has_node = 0, has_at = 0;
    for (char *item = buf;;) {
        char *comma = strchr(item, ',');
        if (comma) *comma = 0;
        char *eq = strchr(item, '=');
        if (!eq || eq == item || !eq[1]) return bad(err, cap, "bad setup item (use key=value):", item);
        *eq = 0;
        const char *k = item, *v = eq + 1;
        if (strcmp(k, "level") == 0) {
            if (s.has_level) return bad(err, cap, "duplicate key", k);
            const int i = sm64_level_find(v);
            if (i < 0) return bad(err, cap, "unknown level (grounds bob wf jrb ccm bbh hmc lll ssl ddd sl wdw ttm thi ttc rr):", v);
            s.has_level = 1; s.level_index = i;
            if (!comma) break;
            item = comma + 1;
            continue;
        }
        if (strcmp(k, "mario_cm") == 0) {
            if (s.has_mario_cm) return bad(err, cap, "duplicate key", k);
            int digits = 0, points = 0;
            for (const char *c = v; *c; c++) {
                if (isdigit((unsigned char)*c)) digits++;
                else if (*c == '.') points++;
                else return bad(err, cap, "bad number for", k);
            }
            if (!digits || points > 1) return bad(err, cap, "bad number for", k);
            const float cm = strtof(v, NULL);
            if (!(cm >= SM64_MARIO_CM_MIN && cm <= SM64_MARIO_CM_MAX)) return bad(err, cap, "mario_cm must be 0.5..10:", v);
            s.has_mario_cm = 1; s.mario_cm = cm;
            if (!comma) break;
            item = comma + 1;
            continue;
        }
        if (strcmp(k, "cannon") == 0) {
            if (s.has_cannon) return bad(err, cap, "duplicate key", k);
            if (strcmp(v, "0") != 0 && strcmp(v, "1") != 0) return bad(err, cap, "cannon must be 0 or 1:", v);
            s.has_cannon = 1; s.cannon = v[0] == '1';
            if (!comma) break;
            item = comma + 1;
            continue;
        }
        if (strcmp(k, "rooms") == 0) {
            if (s.has_rooms) return bad(err, cap, "duplicate key", k);
            if (strcmp(v, "all") != 0 && strcmp(v, "own") != 0) return bad(err, cap, "rooms must be all or own:", v);
            s.has_rooms = 1; s.draw_all_rooms = strcmp(v, "all") == 0;
            if (!comma) break;
            item = comma + 1;
            continue;
        }
        if (strcmp(k, "other_rooms") == 0) {
            if (s.has_other_rooms) return bad(err, cap, "duplicate key", k);
            const int m = strcmp(v, "hidden") == 0 ? SM64_OTHER_ROOMS_HIDDEN : strcmp(v, "frozen") == 0 ? SM64_OTHER_ROOMS_FROZEN
                        : strcmp(v, "live") == 0 ? SM64_OTHER_ROOMS_LIVE : -1;
            if (m < 0) return bad(err, cap, "other_rooms must be hidden, frozen or live:", v);
            s.has_other_rooms = 1; s.other_rooms = m;
            if (!comma) break;
            item = comma + 1;
            continue;
        }
        if (strcmp(k, "start") == 0) {
            if (s.has_start) return bad(err, cap, "duplicate key", k);
            if (strcmp(v, "story") != 0 && strcmp(v, "level") != 0) return bad(err, cap, "start must be story or level:", v);
            s.has_start = 1; s.start_story = strcmp(v, "story") == 0;
            if (!comma) break;
            item = comma + 1;
            continue;
        }
        if (isspace((unsigned char)v[0]) || v[0] == '-' || v[0] == '+') return bad(err, cap, "bad number for", k);
        const int base = (v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) ? 16 : 10;
        char *end = NULL;
        errno = 0;
        const unsigned long long n = strtoull(v, &end, base);
        if (!end || *end || errno) return bad(err, cap, "bad number for", k);
        if (strcmp(k, "stars") == 0) {
            if (s.has_stars) return bad(err, cap, "duplicate key", k);
            if (n > 0x7Full) return bad(err, cap, "stars must be 0..0x7F:", v);
            s.has_stars = 1; s.stars = (uint32_t)n;
        } else if (strcmp(k, "lives") == 0) {
            if (s.has_lives) return bad(err, cap, "duplicate key", k);
            if (n > 99ull) return bad(err, cap, "lives must be 0..99:", v);
            s.has_lives = 1; s.lives = (int)n;
        } else if (strcmp(k, "kill-at") == 0) {
            if (s.has_kill_at) return bad(err, cap, "duplicate key", k);
            if (n > 0xFFFFFFFFull) return bad(err, cap, "kill-at out of range:", v);
            s.has_kill_at = 1; s.kill_at = (uint32_t)n;
        } else if (strcmp(k, "act") == 0) {
            if (s.has_act) return bad(err, cap, "duplicate key", k);
            if (n < 1ull || n > 6ull) return bad(err, cap, "act must be 1..6:", v);
            s.has_act = 1; s.act = (int)n;
        } else if (strcmp(k, "warp-node") == 0) {
            if (has_node) return bad(err, cap, "duplicate key", k);
            if (n > 255ull) return bad(err, cap, "warp-node must be 0..255:", v);
            has_node = 1; s.warp_node = (int)n;
        } else if (strcmp(k, "warp-at") == 0) {
            if (has_at) return bad(err, cap, "duplicate key", k);
            if (n > 0xFFFFFFFFull) return bad(err, cap, "warp-at out of range:", v);
            has_at = 1; s.warp_at = (uint32_t)n;
        } else {
            return bad(err, cap, "unknown setup key (level, act, stars, lives, kill-at, warp-node, warp-at, mario_cm, cannon, rooms, other_rooms, start):", k);
        }
        if (!comma) break;
        item = comma + 1;
    }
    if (has_node != has_at) return bad(err, cap, "warp-node and warp-at go together:", has_node ? "warp-node" : "warp-at");
    s.has_warp = has_node;
    if (s.has_start && s.start_story && (s.has_level || s.has_act || s.has_stars))
        return bad(err, cap, "start=story takes no level, act or stars (the file select picks the save):",
                   s.has_level ? "level" : s.has_act ? "act" : "stars");
    if (s.has_start && s.start_story && (s.has_kill_at || s.has_warp))
        return bad(err, cap, "start=story takes no kill-at, warp-node or warp-at (level-start test items):",
                   s.has_kill_at ? "kill-at" : "warp-node");
    const sm64_level_entry *start = sm64_level_at(s.has_level ? s.level_index : SM64_LEVEL_BOB_INDEX);
    if (!start->course && s.has_act) return bad(err, cap, "act needs a course, not", start->key);
    if (!start->course && s.has_stars) return bad(err, cap, "stars needs a course, not", start->key);
    *out = s;
    return 1;
}
float sm64_setup_mario_cm(const char *text) {
    sm64_setup s;
    return sm64_setup_parse(text, &s, NULL, 0) && s.has_mario_cm ? s.mario_cm : NAN;
}
int sm64_mario_cm_setup_item(float cm, char *buf, size_t cap) {
    if (buf && cap) buf[0] = 0;
    const float c = sm64_mario_cm_clamp(cm);
    if (c == SM64_MARIO_CM_DEFAULT || !buf) return 0;
    for (int prec = 1; prec <= 9; prec++) {   /* the shortest that reads back exactly ("%g" never needs an exponent here) */
        char num[24];
        snprintf(num, sizeof num, "%.*g", prec, (double)c);
        if (strchr(num, 'e') || strtof(num, NULL) != c) continue;
        const int n = snprintf(buf, cap, "mario_cm=%s", num);
        if (n < 0 || (size_t)n >= cap) { buf[0] = 0; return 0; }
        return 1;
    }
    return 0;
}
int sm64_setup_cannon(const char *text) {
    sm64_setup s;
    return sm64_setup_parse(text, &s, NULL, 0) && s.has_cannon ? s.cannon : -1;
}
int sm64_cannon_setup_item(int on, char *buf, size_t cap) {
    if (buf && cap) buf[0] = 0;
    if (on || !buf || cap < sizeof "cannon=0") return 0;
    snprintf(buf, cap, "cannon=0");
    return 1;
}
int sm64_setup_rooms(const char *text) {
    sm64_setup s;
    return sm64_setup_parse(text, &s, NULL, 0) && s.has_rooms ? s.draw_all_rooms : -1;
}
int sm64_setup_other_rooms(const char *text) {
    sm64_setup s;
    return sm64_setup_parse(text, &s, NULL, 0) && s.has_other_rooms ? s.other_rooms : -1;
}
int sm64_game_setting_items(float mario_cm, int cannon, int draw_all_rooms, int other_rooms, char *buf, size_t cap) {
    static const char *const kOther[] = { "hidden", "frozen", "live" };
    if (!buf || !cap) return -1;
    buf[0] = 0;
    char items[4][32] = { "", "", "", "" };
    sm64_mario_cm_setup_item(mario_cm, items[0], sizeof items[0]);
    sm64_cannon_setup_item(cannon, items[1], sizeof items[1]);
    if (!draw_all_rooms) snprintf(items[2], sizeof items[2], "rooms=own");
    if (other_rooms >= SM64_OTHER_ROOMS_HIDDEN && other_rooms < SM64_OTHER_ROOMS_LIVE)
        snprintf(items[3], sizeof items[3], "other_rooms=%s", kOther[other_rooms]);
    int n = 0;
    size_t len = 0;
    for (int i = 0; i < 4; i++) {
        if (!items[i][0]) continue;
        const int w = snprintf(buf + len, cap - len, "%s%s", n ? "," : "", items[i]);
        if (w < 0 || (size_t)w >= cap - len) { buf[0] = 0; return -1; }
        len += (size_t)w; n++;
    }
    return n;
}

int sm64_setup_start(const char *text) {
    sm64_setup s;
    return sm64_setup_parse(text, &s, NULL, 0) && s.has_start ? s.start_story : -1;
}
int sm64_start_resolve(const char *launch_setup, int saved_story, const char **why) {
    sm64_setup s;
    const int ok = sm64_setup_parse(launch_setup, &s, NULL, 0);
    const char *w = "saved";
    int story = saved_story ? 1 : 0;
    if (ok && s.has_start) { w = "launch"; story = s.start_story; }
    else if (ok && (s.has_level || s.has_act || s.has_stars)) { w = "launch level="; story = 0; }
    if (why) *why = w;
    return story;
}
