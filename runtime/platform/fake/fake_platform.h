#pragma once
#include "ar_platform.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct fake_platform {
    ar_platform api;
    int64_t time_ns, step_ns;
    uint32_t view_count;
    float ipd;
    ar_fov fov;
    uint32_t width, height;
    ar_pose head;
    ar_pose table;
    int table_valid;
    float table_half_extent[2];
    float near_m;
    uint8_t head_tracked;
    float display_hz;
    uint32_t begun, submitted;
    ar_submit_info last_submit;
    ar_pad pad;
    int pad_connected;
    ar_session_state state;
    uint32_t frames;
    uint32_t stop_after;
} fake_platform;
void fake_platform_init(fake_platform *fp, double display_hz);
#ifdef __cplusplus
}
#endif
