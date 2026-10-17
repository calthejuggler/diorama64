#include "sm64_relevance.h"
#include <math.h>
float sm64_relevance_gain(float d, float floor_db) {
    if (!(floor_db < 0.f) || !(d > SM64_RELEVANCE_NEAR)) { return 1.f; }
    float f = (d - SM64_RELEVANCE_NEAR) / (SM64_RELEVANCE_FAR - SM64_RELEVANCE_NEAR);
    if (f > 1.f) { f = 1.f; }
    return (float)pow(10.0, (double)(floor_db * f) / 20.0);
}
float sm64_player_focus_floor_db(float s) {
    if (!(s > 0.f)) { return 0.f; }
    return SM64_PLAYER_FOCUS_MAX_DB * (s < 1.f ? s : 1.f);
}
