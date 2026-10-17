#pragma once
#include <mutex>
#include <string>
extern "C" {
#include "ar_platform.h"
}
namespace ar {
// Offscreen display time of output frame `frame` at `fps`: ceil(frame/fps s) in ns. The runtime
// measures time from frame 0 and floors dt*30/1e9, so a time truncated below a 1/30 s boundary repeats a tick; a
// constant "+1" cancels against frame 0's own +1, while rounding up lands each frame on or just after its boundary.
inline int64_t offscreen_frame_time_ns(uint32_t frame, uint32_t fps) { return ((int64_t)frame * 1000000000LL + fps - 1) / fps; }
inline uint64_t offscreen_ticks_expected(uint32_t frames, uint32_t fps) { return frames ? (uint64_t)(frames - 1) * 30 / fps + 1 : 0; }
class HarnessPlatform {
public:
    HarnessPlatform();
    ~HarnessPlatform();
    const ar_platform *api() const { return &api_; }
    void set_size(uint32_t w, uint32_t h);
    void set_stereo(bool on);
    bool stereo() const;
    void set_head(const ar_pose &tracking_from_head);
    void set_fov_up(float tan_half);
    void set_near(float near_m);
    void set_time_ns(int64_t t);
    void poll_gamepad();
    void set_fake_probe(void *mtl_device, const float dir[3], const float rgb[3]);
    void set_preview_test(const std::string &mode);
private:
    static int wait_frame(void *u, ar_frame_info *out);
    static int get_table(void *u, ar_table *out);
    static int get_pad(void *u, uint32_t player, ar_pad *out);
    static ar_session_state state(void *u);
    static int get_environment(void *u, ar_environment *out);
    static int get_preview(void *u, ar_preview *out);
    ar_platform api_{};
    mutable std::mutex m_;
    ar_pose head_{{0.f, 1.55f, 1.9f}, {0.f, 0.f, 0.f, 1.f}};
    ar_pose table_{{0.f, 0.75f, 0.f}, {0.f, 0.f, 0.f, 1.f}};
    uint32_t w_ = 1280, h_ = 720;
    float fov_up_ = 0.5f;
    float near_m_ = 0.f;
    bool stereo_ = false;
    int64_t time_ = 0;
    ar_pad pad_{};
    bool pad_connected_ = false;
    void *fake_cube_ = nullptr;
    uint32_t fake_gen_ = 0;
    int preview_mode_ = AR_PREVIEW_NONE;
};
}
