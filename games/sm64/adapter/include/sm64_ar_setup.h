#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct sm64_setup { int has_stars; uint32_t stars; int has_lives; int lives; int has_kill_at; uint32_t kill_at; int has_level; int level_index; int has_act; int act;
                            int has_warp; int warp_node; uint32_t warp_at; int has_mario_cm; float mario_cm;
                            int has_cannon; int cannon; int has_rooms; int draw_all_rooms; int has_other_rooms; int other_rooms;
                            int has_start; int start_story; } sm64_setup;
/* Numbers decimal or 0x hex (a leading 0 is decimal, not octal; no sign, no spaces); empty items are errors. */
int sm64_setup_parse(const char *text, sm64_setup *out, char *err, size_t err_cap);
float sm64_setup_mario_cm(const char *text);
/* The shortest decimal that parses back to exactly cm, so the replay runs at the recorded scale bit for bit; nothing at
 * the default 2 cm. */
int sm64_mario_cm_setup_item(float cm, char *buf, size_t cap);
int sm64_setup_cannon(const char *text);
int sm64_cannon_setup_item(int on, char *buf, size_t cap);
int sm64_setup_rooms(const char *text);
int sm64_setup_other_rooms(const char *text);
int sm64_setup_start(const char *text);
int sm64_start_resolve(const char *launch_setup, int saved_story, const char **why);
int sm64_game_setting_items(float mario_cm, int cannon, int draw_all_rooms, int other_rooms, char *buf, size_t cap);
#ifdef __cplusplus
}
#endif
