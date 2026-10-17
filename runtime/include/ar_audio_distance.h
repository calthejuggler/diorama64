/* runtime/include/ar_audio_distance.h — the distance gain every spatializer of this project uses (the PHASE envelope, the
 * offline binaural renderer): 1 within 0.25 m, then 1 / (1 + 1.5 (d - 0.25)) at the points, linear in
 * between, flat after the last. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#define AR_AUDIO_DISTANCE_POINTS 9
static const float ar_audio_distance_curve[AR_AUDIO_DISTANCE_POINTS][2] = {
    {0.00f, 1.0000f}, {0.25f, 1.0000f}, {0.50f, 0.7273f}, {1.00f, 0.4706f}, {1.50f, 0.3478f},
    {2.00f, 0.2759f}, {3.00f, 0.1951f}, {4.00f, 0.1509f}, {6.00f, 0.1039f},
};
float ar_audio_distance_gain(float distance_m);
#ifdef __cplusplus
}
#endif
