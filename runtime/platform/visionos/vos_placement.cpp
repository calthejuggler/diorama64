#include "vos_placement.h"
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unistd.h>
namespace ar {
ar_pose vos_table_from_head(const ar_pose &head, const PlaceParams &p) {
    const float f0[3] = {0.f, 0.f, -1.f}, u0[3] = {0.f, 1.f, 0.f};
    float f[3], u[3];
    ar_quat_rotate(head.quat, f0, f);
    ar_quat_rotate(head.quat, u0, u);
    double fx = f[0], fz = f[2];
    if (std::hypot(fx, fz) < 1e-3) {
        const double s = f[1] < 0.f ? 1.0 : -1.0;
        fx = s * u[0]; fz = s * u[2];
    }
    const double len = std::hypot(fx, fz);
    fx /= len; fz /= len;
    // Never atan2f: the game's global atan2f replaces libm's in the app.
    const double heading = std::atan2(-fx, -fz) + (double)p.yaw_deg * M_PI / 180.0;
    ar_pose t;
    t.pos[0] = head.pos[0] + (float)(fx * p.forward_m);
    t.pos[1] = head.pos[1] - p.below_m;
    t.pos[2] = head.pos[2] + (float)(fz * p.forward_m);
    t.quat[0] = 0.f; t.quat[1] = (float)std::sin(heading / 2); t.quat[2] = 0.f; t.quat[3] = (float)std::cos(heading / 2);
    return t;
}
std::string placement_to_json(const PlacementRecord &r) {
    char b[512];
    const ar_pose &t = r.tracking_from_table;
    std::snprintf(b, sizeof b,
                  "{\"version\": 1, \"anchor\": \"%s\", \"game\": \"%s\", \"yaw_deg\": %.6f, \"pos\": [%.6f, %.6f, %.6f], "
                  "\"quat\": [%.6f, %.6f, %.6f, %.6f]}\n",
                  r.anchor.c_str(), r.game.c_str(), (double)r.yaw_deg, (double)t.pos[0], (double)t.pos[1], (double)t.pos[2],
                  (double)t.quat[0], (double)t.quat[1], (double)t.quat[2], (double)t.quat[3]);
    return b;
}
namespace {
bool value_at(const std::string &t, const char *key, size_t *pos) {
    const std::string k = std::string("\"") + key + "\"";
    size_t p = t.find(k);
    if (p == std::string::npos) return false;
    p = t.find(':', p + k.size());
    if (p == std::string::npos) return false;
    for (p++; p < t.size() && std::isspace((unsigned char)t[p]); p++) {}
    *pos = p;
    return p < t.size();
}
bool get_string(const std::string &t, const char *key, std::string *out) {
    size_t p;
    if (!value_at(t, key, &p) || t[p] != '"') return false;
    const size_t e = t.find('"', p + 1);
    if (e == std::string::npos) return false;
    *out = t.substr(p + 1, e - p - 1);
    return out->find('\\') == std::string::npos;
}
bool number(const std::string &t, size_t *p, double *out) {
    const char *s = t.c_str() + *p;
    char *end = nullptr;
    *out = std::strtod(s, &end);
    if (end == s || !std::isfinite(*out)) return false;
    *p += (size_t)(end - s);
    return true;
}
bool get_number(const std::string &t, const char *key, double *out) { size_t p; return value_at(t, key, &p) && number(t, &p, out); }
bool get_array(const std::string &t, const char *key, double *out, int n) {
    size_t p;
    if (!value_at(t, key, &p) || t[p] != '[') return false;
    p++;
    for (int i = 0; i < n; i++) {
        while (p < t.size() && std::isspace((unsigned char)t[p])) p++;
        if (!number(t, &p, &out[i])) return false;
        while (p < t.size() && std::isspace((unsigned char)t[p])) p++;
        if (p >= t.size() || t[p] != (i + 1 < n ? ',' : ']')) return false;
        p++;
    }
    return true;
}
bool valid_anchor(const std::string &a) {
    if (a == "fixed") return true;
    if (a.size() != 36) return false;
    for (size_t i = 0; i < a.size(); i++) {
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? a[i] != '-' : !std::isxdigit((unsigned char)a[i])) return false;
    }
    return true;
}
bool valid_game(const std::string &g) {
    if (g.empty()) return false;
    for (char c : g) if (!(std::islower((unsigned char)c) || std::isdigit((unsigned char)c) || c == '_')) return false;
    return true;
}
}
bool placement_from_json(const std::string &t, PlacementRecord *out, std::string *err) {
    PlacementRecord r;
    double version = 0, yaw = 0, pos[3], q[4];
    if (!get_number(t, "version", &version) || version != 1.0) { *err = "placement: not a version 1 record"; return false; }
    if (!get_string(t, "anchor", &r.anchor) || !valid_anchor(r.anchor)) { *err = "placement: bad anchor"; return false; }
    if (!get_string(t, "game", &r.game) || !valid_game(r.game)) { *err = "placement: bad game"; return false; }
    if (!get_number(t, "yaw_deg", &yaw)) { *err = "placement: bad yaw_deg"; return false; }
    if (!get_array(t, "pos", pos, 3)) { *err = "placement: bad pos"; return false; }
    if (!get_array(t, "quat", q, 4)) { *err = "placement: bad quat"; return false; }
    if (std::fabs(pos[0]) > 100 || std::fabs(pos[1]) > 100 || std::fabs(pos[2]) > 100) { *err = "placement: pos out of range"; return false; }
    const double n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (std::fabs(n - 1.0) > 1e-3) { *err = "placement: quaternion is not unit length"; return false; }
    r.yaw_deg = (float)yaw;
    for (int k = 0; k < 3; k++) r.tracking_from_table.pos[k] = (float)pos[k];
    for (int k = 0; k < 4; k++) r.tracking_from_table.quat[k] = (float)(q[k] / n);
    *out = r;
    return true;
}
bool placement_save(const std::string &path, const PlacementRecord &r, std::string *err) {
    const std::string tmp = path + ".tmp";
    {   // flushed to storage before the rename: a device app can be killed at any time
        const std::string text = placement_to_json(r);
        FILE *f = std::fopen(tmp.c_str(), "wb");
        if (!f) { *err = "placement: cannot write " + tmp; return false; }
        const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size() && std::fflush(f) == 0 && ::fsync(fileno(f)) == 0;
        if (std::fclose(f) != 0 || !ok) { std::remove(tmp.c_str()); *err = "placement: cannot write " + tmp; return false; }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) { *err = "placement: cannot rename to " + path; return false; }
    return true;
}
bool placement_load(const std::string &path, PlacementRecord *out, std::string *err) {
    std::ifstream f(path);
    if (!f) { *err = "placement: no record at " + path; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    return placement_from_json(ss.str(), out, err);
}
bool placement_anchor_applies(const PlacementRecord &r, const std::string &uuid) {
    return r.anchor != "fixed" && !r.anchor.empty() && uuid == r.anchor;
}
void placement_forget(PlacementRecord *r, std::string *stale) {
    if (stale->empty()) *stale = placement_stale_anchor(*r);
    *r = PlacementRecord{};
}
std::string placement_stale_anchor(const PlacementRecord &replaced) {
    return replaced.anchor == "fixed" ? std::string() : replaced.anchor;
}
bool placement_reset(const std::string &path, std::string *stale_anchor, std::string *err) {
    PlacementRecord r;
    std::string ignored;
    *stale_anchor = placement_load(path, &r, &ignored) ? placement_stale_anchor(r) : std::string();
    if (std::remove(path.c_str()) != 0 && errno != ENOENT) {
        *err = "placement: cannot delete " + path + ": " + std::strerror(errno);
        return false;
    }
    return true;
}
}
