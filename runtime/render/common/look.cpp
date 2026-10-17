#include "look.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
extern "C" {
#include "fc_frame.h"
}
namespace ar {
namespace {
struct Row {
    const char *key, *def;
    bool (*set)(LookSettings &, const std::string &);
    std::string (*get)(const LookSettings &);
};
bool num(const std::string &v, float lo, float hi, float *out) {
    if (v.empty() || std::isspace((unsigned char)v[0])) return false;
    char *end = nullptr;
    const double d = std::strtod(v.c_str(), &end);
    if (end != v.c_str() + v.size() || !std::isfinite(d)) return false;
    // Bounds are floats (0.3f > 0.3 as doubles): text within a hair of a bound — the bound itself as typed, or a
    // Float neighbour printed as Double — clamps to it; anything further out is rejected.
    const double tol = 1e-6 * ((double)hi - (double)lo);
    if (d < (double)lo - tol || d > (double)hi + tol) return false;
    *out = d < lo ? lo : d > hi ? hi : (float)d;
    return true;
}
std::string fmt(float f) { char b[32]; std::snprintf(b, sizeof b, "%g", (double)f); return b; }
bool pick(const std::string &v, std::initializer_list<const char *> names, uint8_t *out) {
    uint8_t i = 0;
    for (const char *n : names) { if (v == n) { *out = i; return true; } i++; }
    return false;
}
const char *name_of(std::initializer_list<const char *> names, uint8_t i) {
    uint8_t k = 0;
    for (const char *n : names) { if (k == i) return n; k++; }
    return "";
}
#define LOOK_PICK(field, ...)                                                                              \
    [](LookSettings &s, const std::string &v) { return pick(v, {__VA_ARGS__}, &s.field); },             \
        [](const LookSettings &s) { return std::string(name_of({__VA_ARGS__}, s.field)); }
bool flag(const std::string &v, bool *out) { if (v != "0" && v != "1") return false; *out = v == "1"; return true; }
#define LOOK_FLAG(field) \
    [](LookSettings &s, const std::string &v) { return flag(v, &s.field); }, [](const LookSettings &s) { return std::string(s.field ? "1" : "0"); }
#define LOOK_NUM(field, lo, hi) \
    [](LookSettings &s, const std::string &v) { return num(v, lo, hi, &s.field); }, [](const LookSettings &s) { return fmt(s.field); }
const std::vector<Row> &rows() {
    static const std::vector<Row> r = {
        {"mode", "roomlit",
         [](LookSettings &s, const std::string &v) {
             if (v != "original" && v != "roomlit") return false;
             s.roomlit = v == "roomlit";
             return true;
         },
         [](const LookSettings &s) { return std::string(s.roomlit ? "roomlit" : "original"); }},
        {"key_az", "45", LOOK_NUM(key_az_deg, 0.f, 360.f)},
        {"key_el", "55", LOOK_NUM(key_el_deg, 10.f, 90.f)},
        {"key_power", "1", LOOK_NUM(key_power, 0.3f, 1.5f)},
        {"warmth", "0", LOOK_NUM(warmth, -1.f, 1.f)},
        {"source", "room", LOOK_PICK(source, "room", "fixed")},
        {"match_brightness", "1", LOOK_NUM(match_brightness, 0.f, 1.f)},
        {"match_colour", "1", LOOK_NUM(match_colour, 0.f, 1.f)},
        {"neutral_lum", "0", LOOK_NUM(neutral_lum, 0.f, 1e6f)},
        {"light_response", "steady", LOOK_PICK(light_response, "steady", "responsive")},
        {"contact", "1", LOOK_FLAG(contact)},
        {"contact_strength", "0.5", LOOK_NUM(contact_strength, 0.f, 1.f)},
        {"shadows", "all", LOOK_PICK(shadows, "off", "objects", "all")},
        {"blob", "hide", LOOK_PICK(blob, "keep", "hide")},
        {"softness", "0.5", LOOK_NUM(softness, 0.f, 1.f)},
        {"directional", "0.6", LOOK_NUM(directional, 0.f, 1.f)},
        {"table_cast", "0.6", LOOK_NUM(table_cast, 0.f, 1.f)},
        {"relight", "1", LOOK_FLAG(relight)},
        {"relight_strength", "0.7", LOOK_NUM(relight_strength, 0.f, 1.f)},
        {"ao", "1", LOOK_FLAG(ao)},
        {"ao_strength", "0.6", LOOK_NUM(ao_strength, 0.f, 1.f)},
        {"toy", "1", LOOK_FLAG(toy)},
        {"gloss", "0.12", LOOK_NUM(gloss, 0.f, 1.f)},
        {"rim", "0.08", LOOK_NUM(rim, 0.f, 1.f)},
        {"reflections", "1", LOOK_FLAG(reflections)},
        {"msaa", "4",
         [](LookSettings &s, const std::string &v) {
             if (v != "1" && v != "2" && v != "4") return false;
             s.msaa = (uint8_t)(v[0] - '0');
             return true;
         },
         [](const LookSettings &s) { return std::to_string(s.msaa); }},
        {"foveation", "1", LOOK_FLAG(foveation)},
        {"render_quality", "0", LOOK_NUM(render_quality, 0.f, 1.f)},
        {"tex_filter", "smooth",
         [](LookSettings &s, const std::string &v) {
             if (v != "smooth" && v != "vanilla") return false;
             s.tex_smooth = v == "smooth";
             return true;
         },
         [](const LookSettings &s) { return std::string(s.tex_smooth ? "smooth" : "vanilla"); }},
        {"decal_fix", "1", LOOK_FLAG(decal_fix)},
        {"edge_aa", "1", LOOK_FLAG(edge_aa)},
        {"colour", "auto", LOOK_PICK(colour, "auto", "linear", "raw")},
    };
    return r;
}
const Row *find(const std::string &key) {
    for (const Row &r : rows()) if (key == r.key) return &r;
    return nullptr;
}
}
const std::vector<std::string> &look_keys() {
    static const std::vector<std::string> k = [] { std::vector<std::string> v; for (const Row &r : rows()) v.push_back(r.key); return v; }();
    return k;
}
uint8_t look_normal_flags(const LookSettings &s) {
    if (!s.roomlit) return 0;
    return (uint8_t)((s.relight || s.shadows != 0 ? FC_LF_BAKED | FC_LF_GAME_LIT : 0) | (s.toy ? FC_LF_GAME_LIT : 0));
}
bool look_default(const std::string &key, std::string *value) {
    const Row *r = find(key);
    if (r && value) *value = r->def;
    return r != nullptr;
}
namespace {
struct Superseded { int rev; const char *key, *old; };
// Every default that changed, with the revision that changed it (append only; look_defaults_rev() is the last revision).
const Superseded kSuperseded[] = {
    {1, "shadows", "objects"}, {1, "blob", "keep"}, {1, "toy", "0"},
    {2, "gloss", "0.4"}, {2, "rim", "0.3"},
};
}
int look_defaults_rev() {
    int r = 0;
    for (const Superseded &x : kSuperseded) r = x.rev > r ? x.rev : r;
    return r;
}
bool look_drop_stored(const std::string &key, const std::string &stored, int stored_rev, bool has_launch_override) {
    if (has_launch_override) return false;
    for (const Superseded &x : kSuperseded)
        if (x.rev > stored_rev && key == x.key && stored == x.old) return true;
    return false;
}
bool look_set(LookSettings *s, const std::string &key, const std::string &value, std::string *err) {
    const Row *r = find(key);
    if (!r) { if (err) *err = "unknown look key " + key; return false; }
    LookSettings t = *s;
    if (!r->set(t, value)) { if (err) *err = "bad value '" + value + "' for " + key; return false; }
    *s = t;
    return true;
}
std::string look_get(const LookSettings &s, const std::string &key) { const Row *r = find(key); return r ? r->get(s) : std::string(); }
bool LookStore::set(const std::string &key, const std::string &value, std::string *err) {
    std::lock_guard<std::mutex> g(m_);
    if (!look_set(&s_, key, value, err)) return false;
    gen_++;
    return true;
}
void LookStore::replace(const LookSettings &s) { std::lock_guard<std::mutex> g(m_); s_ = s; gen_++; }
LookSettings LookStore::get(uint64_t *generation) const {
    std::lock_guard<std::mutex> g(m_);
    if (generation) *generation = gen_;
    return s_;
}
uint64_t LookStore::generation() const { std::lock_guard<std::mutex> g(m_); return gen_; }
}
