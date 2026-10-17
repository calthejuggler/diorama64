#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>
extern "C" {
#include "ar_audio_tuning.h"
#include "ar_audio_types.h"
#include "ar_runtime.h"
}
namespace ar {
class ScenePositions {
public:
    static constexpr size_t kHistory = 32;
    void push(const ar_audio_tick_meta &m);
    bool at(uint64_t frame, ar_audio_source out[AR_AUDIO_MAX_SOURCES], ar_pose *listener) const;
private:
    std::deque<ar_audio_tick_meta> h_;
};
float phase_lane_gain(const ar_audio_tuning &t, const ar_pose &head, const ar_audio_source &lane, float out_pos[3],
                      float *level = nullptr);
// Double atan2: the game's atan2f replaces libm's in the app.
struct ListenerDirection { double distance_m = 0.0, azimuth_deg = 0.0, elevation_deg = 0.0; };
ListenerDirection listener_direction(const ar_pose &listener, const float point[3]);
enum class AudioBackend { none, remoteio, phase };
AudioBackend choose_audio_backend(const std::string &requested, bool audio_on, bool phase_started);
uint32_t audio_mode_for(AudioBackend b);
struct AudioDecision {
    AudioBackend backend = AudioBackend::none;
    bool stereo_only_adapter = false;
    bool fell_back = false;
    std::string error;
};
AudioDecision decide_audio_backend(ar_runtime *rt, const std::string &requested, bool audio_on,
                                   const std::function<bool(std::string *)> &start_phase);
int shown_audio_state(int game_state, bool audio_spatial);
bool automatic_head_tracking(const std::string &option, bool simulator);
// client = the system's renderer (visionOS 26), local = PHASE rendering in our process. automatic_head flags are set
// before the listener is added anywhere.
enum class PhaseRung { client, local };
struct PhaseRungPlan { PhaseRung rung; bool automatic_head; bool listener_child; };
// On a device the client rung leaves the shared listener unparented (the device refused it as a child).
std::vector<PhaseRungPlan> phase_rungs(const std::string &mode, const std::string &phase_head, bool simulator, bool client_available);
const char *phase_rung_name(PhaseRung r);
std::vector<PhaseRungPlan> phase_rungs_after(const std::vector<PhaseRungPlan> &plan, PhaseRung failed);
}
