#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#define SM64_RELEVANCE_NEAR 2000.f
#define SM64_RELEVANCE_FAR 10000.f
#define SM64_PLAYER_FOCUS_MAX_DB -18.f
float sm64_relevance_gain(float distance_units, float floor_db);
float sm64_player_focus_floor_db(float strength);
#ifdef __cplusplus
}
#endif
