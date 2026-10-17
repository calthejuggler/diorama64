#include "vos_scene_pos.h"
#include <algorithm>
#include <cmath>
extern "C" {
#include "ar_math.h"
}
namespace ar {
void ScenePositions::push(const ar_audio_tick_meta &m) {
    h_.push_back(m);
    while (h_.size() > kHistory) h_.pop_front();
}
bool ScenePositions::at(uint64_t frame, ar_audio_source out[AR_AUDIO_MAX_SOURCES], ar_pose *listener) const {
    if (h_.empty()) return false;
    size_t k = 0;
    for (size_t i = 0; i < h_.size(); i++) if (h_[i].first_frame <= frame) k = i;
    const ar_audio_tick_meta &a = h_[k];
    const ar_audio_tick_meta *b = k + 1 < h_.size() ? &h_[k + 1] : nullptr;
    // Interpolation runs over a's own frames, not up to b.first_frame: after a gap (metadata lost, ring trimmed) the
    // position reaches b's at a.first_frame + a.frames and holds there until b.
    const double into = frame > a.first_frame ? (double)(frame - a.first_frame) : 0.0;
    const float f = a.frames ? (float)std::min(1.0, into / a.frames) : 0.f;
    for (uint32_t l = 0; l < AR_AUDIO_MAX_SOURCES; l++) {
        out[l] = a.lane[l];
        if (!(a.lane[l].flags & AR_SRC_ACTIVE)) { out[l].flags = 0; continue; }
        if (b && (b->lane[l].flags & AR_SRC_ACTIVE) && b->lane[l].id == a.lane[l].id) {
            for (int c = 0; c < 3; c++) out[l].pos[c] = a.lane[l].pos[c] + (b->lane[l].pos[c] - a.lane[l].pos[c]) * f;
            out[l].gain = a.lane[l].gain + (b->lane[l].gain - a.lane[l].gain) * f;
        }
    }
    if (listener) *listener = a.listener;
    return true;
}
float phase_lane_gain(const ar_audio_tuning &t, const ar_pose &head, const ar_audio_source &lane, float pos[3], float *level) {
    if (!(lane.flags & AR_SRC_ACTIVE)) {
        for (int c = 0; c < 3; c++) pos[c] = lane.pos[c];
        if (level) *level = 0.f;
        return 1.f;
    }
    float gain = ar_audio_lane_gain(&t, &head, &lane, pos);
    if (level) *level = gain;
    if (lane.flags & AR_SRC_NO_DISTANCE_LOSS) {
        const float dx = pos[0] - head.pos[0], dy = pos[1] - head.pos[1], dz = pos[2] - head.pos[2];
        gain /= ar_audio_distance_gain(std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    return gain;
}
AudioBackend choose_audio_backend(const std::string &requested, bool audio_on, bool phase_started) {
    if (!audio_on) return AudioBackend::none;
    return requested == "phase" && phase_started ? AudioBackend::phase : AudioBackend::remoteio;
}
uint32_t audio_mode_for(AudioBackend b) { return b == AudioBackend::phase ? AR_AUDIO_MODE_SOURCES : AR_AUDIO_MODE_STEREO; }
int shown_audio_state(int game_state, bool audio_spatial) { return game_state == 1 && !audio_spatial ? 0 : game_state; }
AudioDecision decide_audio_backend(ar_runtime *rt, const std::string &requested, bool audio_on,
                                   const std::function<bool(std::string *)> &start_phase) {
    AudioDecision d;
    bool phase_ok = false;
    if (audio_on && requested == "phase") {
        if (!ar_runtime_set_audio_mode(rt, audio_mode_for(AudioBackend::phase))) {
            d.stereo_only_adapter = true;
        } else {
            phase_ok = start_phase(&d.error);
            if (!phase_ok) {
                ar_runtime_set_audio_mode(rt, audio_mode_for(AudioBackend::remoteio));
                d.fell_back = true;
            } else {
                d.error.clear();
            }
        }
    }
    d.backend = choose_audio_backend(requested, audio_on, phase_ok);
    return d;
}
bool automatic_head_tracking(const std::string &option, bool simulator) { return option == "auto" && !simulator; }
std::vector<PhaseRungPlan> phase_rungs(const std::string &mode, const std::string &phase_head, bool simulator, bool client_available) {
    std::vector<PhaseRungPlan> v;
    if ((mode == "ladder" || mode == "client") && client_available) v.push_back(PhaseRungPlan{PhaseRung::client, !simulator, simulator});
    if (mode == "ladder" || mode == "local") v.push_back(PhaseRungPlan{PhaseRung::local, automatic_head_tracking(phase_head, simulator), true});
    return v;
}
const char *phase_rung_name(PhaseRung r) { return r == PhaseRung::client ? "client" : "local"; }
std::vector<PhaseRungPlan> phase_rungs_after(const std::vector<PhaseRungPlan> &plan, PhaseRung failed) {
    for (size_t i = 0; i < plan.size(); i++)
        if (plan[i].rung == failed) return std::vector<PhaseRungPlan>(plan.begin() + (long)i + 1, plan.end());
    return {};
}
ListenerDirection listener_direction(const ar_pose &listener, const float point[3]) {
    const float rel[3] = {point[0] - listener.pos[0], point[1] - listener.pos[1], point[2] - listener.pos[2]};
    const float inv[4] = {-listener.quat[0], -listener.quat[1], -listener.quat[2], listener.quat[3]};
    float v[3];
    ar_quat_rotate(inv, rel, v);   // listener space: +X right, +Y up, -Z ahead
    const double x = v[0], y = v[1], z = v[2];
    ListenerDirection d;
    d.distance_m = std::sqrt(x * x + y * y + z * z);
    if (d.distance_m == 0.0) return d;
    const double horiz = std::sqrt(x * x + z * z);
    d.azimuth_deg = horiz > 0.0 ? atan2(x, -z) * (180.0 / M_PI) : 0.0;   // double atan2, never atan2f
    d.elevation_deg = atan2(y, horiz) * (180.0 / M_PI);
    return d;
}
}
