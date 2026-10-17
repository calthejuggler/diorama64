/* Render-only: never a setup item, never recorded, never locked (the game state does not depend on them). Any thread. */
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct sm64_setting_def { const char *key; const char *choices; const char *def; } sm64_setting_def;
int  sm64_settings_count(void);
const sm64_setting_def *sm64_settings_def(int i);
int  sm64_settings_set(const char *key, const char *value, char *err, size_t cap);
int  sm64_settings_get(const char *key, char *buf, size_t cap);
int  sm64_settings_choice(const char *key);
void sm64_settings_reset(void);
int  sm64_settings_revision(void);
int  sm64_settings_drop_stored(const char *key, const char *stored, int stored_rev, int has_launch_override);
#ifdef __cplusplus
}
#endif
