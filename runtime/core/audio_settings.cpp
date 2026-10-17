#include "ar_audio_settings.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
namespace ar {
namespace {
bool num(const std::string &v, float lo, float hi, float *out) {
    if (v.empty() || std::isspace((unsigned char)v[0])) return false;
    char *end = nullptr;
    const double d = std::strtod(v.c_str(), &end);
    if (end != v.c_str() + v.size() || !std::isfinite(d) || d < lo || d > hi) return false;
    *out = (float)d;
    return true;
}
std::string fmt(float f) { char b[32]; std::snprintf(b, sizeof b, "%g", (double)f); return b; }
struct Row {
    const char *key, *def, *range;
    bool (*set)(AudioSettings &, const std::string &);
    std::string (*get)(const AudioSettings &);
};
const Row kRows[] = {
    {"head_focus", "0.5", "0..1", [](AudioSettings &s, const std::string &v) { return num(v, 0.f, 1.f, &s.head_focus); },
     [](const AudioSettings &s) { return fmt(s.head_focus); }},
    {"player_focus", "0.5", "0..1", [](AudioSettings &s, const std::string &v) { return num(v, 0.f, 1.f, &s.player_focus); },
     [](const AudioSettings &s) { return fmt(s.player_focus); }},
    {"object_focus", "0.2", "0..1", [](AudioSettings &s, const std::string &v) { return num(v, 0.f, 1.f, &s.object_focus); },
     [](const AudioSettings &s) { return fmt(s.object_focus); }},
    {"music", "ring", "box or ring",
     [](AudioSettings &s, const std::string &v) {
         if (v != "box" && v != "ring") return false;
         s.music_ring = v == "ring";
         return true;
     },
     [](const AudioSettings &s) { return std::string(s.music_ring ? "ring" : "box"); }},
    {"ring_radius", "1", "0.5..2", [](AudioSettings &s, const std::string &v) { return num(v, 0.5f, 2.f, &s.ring_radius); },
     [](const AudioSettings &s) { return fmt(s.ring_radius); }},
    {"ring_rotate", "0", "0..30", [](AudioSettings &s, const std::string &v) { return num(v, 0.f, 30.f, &s.ring_rotate); },
     [](const AudioSettings &s) { return fmt(s.ring_rotate); }},
    {"ring_height", "head", "head or table",
     [](AudioSettings &s, const std::string &v) {
         if (v != "head" && v != "table") return false;
         s.ring_at_head = v == "head";
         return true;
     },
     [](const AudioSettings &s) { return std::string(s.ring_at_head ? "head" : "table"); }},
    {"ring_focus", "0.2", "0..1", [](AudioSettings &s, const std::string &v) { return num(v, 0.f, 1.f, &s.ring_focus); },
     [](const AudioSettings &s) { return fmt(s.ring_focus); }},
    {"spotlight", "-9", "-30..0 dB", [](AudioSettings &s, const std::string &v) { return num(v, -30.f, 0.f, &s.spotlight_db); },
     [](const AudioSettings &s) { return fmt(s.spotlight_db); }},
    {"spotlight_cone", "30", "5..90 degrees",
     [](AudioSettings &s, const std::string &v) { return num(v, 5.f, 90.f, &s.spotlight_cone); },
     [](const AudioSettings &s) { return fmt(s.spotlight_cone); }},
    {"widen", "1", "1..2", [](AudioSettings &s, const std::string &v) { return num(v, 1.f, 2.f, &s.widen); },
     [](const AudioSettings &s) { return fmt(s.widen); }},
    {"sfx_db", "0", "-12..6 dB", [](AudioSettings &s, const std::string &v) { return num(v, -12.f, 6.f, &s.sfx_db); },
     [](const AudioSettings &s) { return fmt(s.sfx_db); }},
    {"music_db", "0", "-24..6 dB", [](AudioSettings &s, const std::string &v) { return num(v, -24.f, 6.f, &s.music_db); },
     [](const AudioSettings &s) { return fmt(s.music_db); }},
    {"spatializer", "binaural", "binaural, automatic or channel",
     [](AudioSettings &s, const std::string &v) {
         for (AudioSpatializer m : {AudioSpatializer::binaural, AudioSpatializer::automatic, AudioSpatializer::channel})
             if (v == audio_spatializer_name(m)) { s.spatializer = m; return true; }
         return false;
     },
     [](const AudioSettings &s) { return std::string(audio_spatializer_name(s.spatializer)); }},
    {"show_sources", "0", "0 or 1",
     [](AudioSettings &s, const std::string &v) {
         if (v != "0" && v != "1") return false;
         s.show_sources = v == "1";
         return true;
     },
     [](const AudioSettings &s) { return std::string(s.show_sources ? "1" : "0"); }},
};
const Row *row(const std::string &k) {
    for (const Row &r : kRows)
        if (k == r.key) return &r;
    return nullptr;
}
}
const char *audio_spatializer_name(AudioSpatializer m) {
    switch (m) {
    case AudioSpatializer::automatic: return "automatic";
    case AudioSpatializer::channel: return "channel";
    case AudioSpatializer::binaural: break;
    }
    return "binaural";
}
AudioSettings audio_settings_off() {
    AudioSettings s;
    s.head_focus = 0.f;
    s.player_focus = 0.f;
    s.object_focus = 0.f;
    s.music_ring = false;
    s.ring_at_head = false;
    s.ring_focus = 0.f;
    s.spotlight_db = 0.f;
    return s;
}
const std::vector<std::string> &audio_keys() {
    static const std::vector<std::string> k = [] {
        std::vector<std::string> v;
        for (const Row &r : kRows) v.push_back(r.key);
        return v;
    }();
    return k;
}
bool audio_default(const std::string &key, std::string *value) {
    const Row *r = row(key);
    if (!r) return false;
    *value = r->def;
    return true;
}
bool audio_set(AudioSettings *s, const std::string &key, const std::string &value, std::string *err) {
    const Row *r = row(key);
    if (!r) { *err = "unknown key " + key; return false; }
    AudioSettings t = *s;
    if (!r->set(t, value)) { *err = "bad value '" + value + "' for " + key + " (" + r->range + ")"; return false; }
    *s = t;
    return true;
}
std::string audio_get(const AudioSettings &s, const std::string &key) {
    const Row *r = row(key);
    return r ? r->get(s) : std::string();
}
std::string audio_settings_json(const AudioSettings &s) {
    std::string j = "{";
    for (const Row &r : kRows) {
        const std::string v = r.get(s);
        const bool number = !v.empty() && (std::isdigit((unsigned char)v[0]) || v[0] == '-');
        j += (j.size() > 1 ? "," : "") + std::string("\"") + r.key + "\":" + (number ? v : "\"" + v + "\"");
    }
    return j + "}\n";
}
ar_audio_tuning audio_tuning_from_settings(const AudioSettings &s) {
    ar_audio_tuning t{};
    t.distance_exponent = ar_audio_head_focus_exponent(s.head_focus);
    t.spotlight_floor_db = s.spotlight_db;
    t.spotlight_cone_deg = s.spotlight_cone;
    t.widen = s.widen;
    t.sfx_db = s.sfx_db;
    t.music_db = s.music_db;
    ar_audio_tuning_clamp(&t);
    return t;
}
namespace {
struct Preset { const char *name; const char *values[9]; };
const char *const kPresetKeys[10] = {"head_focus", "player_focus", "object_focus", "ring_focus", "spotlight", "spotlight_cone",
                                     "widen", "sfx_db", "music_db", nullptr};
const Preset kPresets[] = {
    {"off", {"0", "0", "0", "0", "0", "30", "1", "0", "0"}},
    {"gentle", {"0.5", "0.5", "0.2", "0.2", "-9", "30", "1", "0", "0"}},
    {"strong", {"1", "1", "0.6", "0.6", "-15", "25", "1.8", "2", "-3"}},
};
}
const std::vector<std::string> &audio_preset_names() {
    static const std::vector<std::string> n = [] {
        std::vector<std::string> v;
        for (const Preset &p : kPresets) v.push_back(p.name);
        return v;
    }();
    return n;
}
bool audio_preset_values(const std::string &name, std::vector<std::pair<std::string, std::string>> *out) {
    for (const Preset &p : kPresets) {
        if (name != p.name) continue;
        out->clear();
        for (const std::string &k : audio_keys())
            for (int i = 0; kPresetKeys[i]; i++)
                if (k == kPresetKeys[i]) out->emplace_back(k, p.values[i]);
        return true;
    }
    return false;
}
bool audio_expand_option(const std::string &key, const std::string &value,
                         std::vector<std::pair<std::string, std::string>> *out, std::string *err) {
    if (key == "preset") {
        if (audio_preset_values(value, out)) return true;
        *err = "bad value '" + value + "' for preset (off, gentle or strong)";
        return false;
    }
    AudioSettings scratch;
    if (!audio_set(&scratch, key, value, err)) return false;
    out->assign(1, {key, value});
    return true;
}
bool AudioStore::set_many(const std::vector<std::pair<std::string, std::string>> &kv, std::string *err) {
    std::lock_guard<std::mutex> g(m_);
    AudioSettings t = s_;
    for (const auto &p : kv)
        if (!audio_set(&t, p.first, p.second, err)) return false;
    s_ = t;
    gen_++;
    return true;
}
bool AudioStore::set(const std::string &k, const std::string &v, std::string *err) {
    std::lock_guard<std::mutex> g(m_);
    if (!audio_set(&s_, k, v, err)) return false;
    gen_++;
    return true;
}
AudioSettings AudioStore::get(uint64_t *gen) const {
    std::lock_guard<std::mutex> g(m_);
    if (gen) *gen = gen_;
    return s_;
}
uint64_t AudioStore::generation() const {
    std::lock_guard<std::mutex> g(m_);
    return gen_;
}
}
