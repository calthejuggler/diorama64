#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
extern "C" {
#include "ar_audio_tuning.h"
}
namespace ar {
// PHASE's output spatializer (PHASEEngine.outputSpatializationMode): binaural even on the built-in speakers (Automatic
// sounded like plain stereo).
enum class AudioSpatializer : uint8_t { binaural, automatic, channel };
const char *audio_spatializer_name(AudioSpatializer m);
struct AudioSettings {
    float head_focus = 0.5f;
    float player_focus = 0.5f;
    float object_focus = 0.2f;
    bool music_ring = true;
    float ring_radius = 1.f;
    float ring_rotate = 0.f;
    bool ring_at_head = true;
    float ring_focus = 0.2f;
    float spotlight_db = -9.f;
    float spotlight_cone = 30.f;
    float widen = 1.f;
    float sfx_db = 0.f;
    float music_db = 0.f;
    AudioSpatializer spatializer = AudioSpatializer::binaural;
    bool show_sources = false;
};
AudioSettings audio_settings_off();
const std::vector<std::string> &audio_keys();
bool audio_default(const std::string &key, std::string *value);
bool audio_set(AudioSettings *s, const std::string &key, const std::string &value, std::string *err);
std::string audio_get(const AudioSettings &s, const std::string &key);
std::string audio_settings_json(const AudioSettings &s);
ar_audio_tuning audio_tuning_from_settings(const AudioSettings &s);
const std::vector<std::string> &audio_preset_names();
bool audio_preset_values(const std::string &name, std::vector<std::pair<std::string, std::string>> *out);
bool audio_expand_option(const std::string &key, const std::string &value,
                         std::vector<std::pair<std::string, std::string>> *out, std::string *err);
// The settings shared between the UI thread (set) and the layer thread (get when generation() changed).
class AudioStore {
public:
    bool set(const std::string &key, const std::string &value, std::string *err);
    bool set_many(const std::vector<std::pair<std::string, std::string>> &kv, std::string *err);
    AudioSettings get(uint64_t *generation = nullptr) const;
    uint64_t generation() const;
private:
    mutable std::mutex m_;
    AudioSettings s_;
    uint64_t gen_ = 1;
};
}
