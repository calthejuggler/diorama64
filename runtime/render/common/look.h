// LookStore is thread-safe; the rest is pure.
#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
namespace ar {
struct LookSettings {
    bool roomlit = true;
    float key_az_deg = 45.f;      // "key_az" 0..360: the fixed key light's azimuth; 0 = from +Z (the viewer's side), 90 = from +X
    float key_el_deg = 55.f;
    float key_power = 1.f;
    float warmth = 0.f;
    uint8_t source = 0;
    float match_brightness = 1.f;
    float match_colour = 1.f;
    float neutral_lum = 0.f;
    enum : uint8_t { kLightSteady = 0, kLightResponsive = 1 };
    uint8_t light_response = kLightSteady;
    bool contact = true;
    float contact_strength = 0.5f;
    uint8_t shadows = 2;
    uint8_t blob = 1;
    float softness = 0.5f;
    float directional = 0.6f;
    float table_cast = 0.6f;
    bool relight = true;
    float relight_strength = 0.7f;
    bool ao = true;
    float ao_strength = 0.6f;
    bool toy = true;
    float gloss = 0.12f;
    float rim = 0.08f;
    bool reflections = true;
    uint8_t msaa = 4;
    // foveation and render_quality are fixed when the layer is configured, so a change applies at the next Open table.
    bool foveation = true;
    float render_quality = 0.f;
    bool tex_smooth = true;
    bool decal_fix = true;
    bool edge_aa = true;
    enum : uint8_t { kColourAuto = 0, kColourLinear = 1, kColourRaw = 2 };
    uint8_t colour = kColourAuto;
};
const std::vector<std::string> &look_keys();
uint8_t look_normal_flags(const LookSettings &s);
bool look_default(const std::string &key, std::string *value);
int look_defaults_rev();
bool look_drop_stored(const std::string &key, const std::string &stored, int stored_rev, bool has_launch_override);
bool look_set(LookSettings *s, const std::string &key, const std::string &value, std::string *err);
std::string look_get(const LookSettings &s, const std::string &key);
// The settings shared between the UI thread (set) and the render thread (get when generation() changed).
class LookStore {
public:
    bool set(const std::string &key, const std::string &value, std::string *err);
    void replace(const LookSettings &s);
    LookSettings get(uint64_t *generation = nullptr) const;
    uint64_t generation() const;
private:
    mutable std::mutex m_;
    LookSettings s_;
    uint64_t gen_ = 1;
};
}
