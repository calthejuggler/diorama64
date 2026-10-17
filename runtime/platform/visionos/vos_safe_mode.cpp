#include "vos_safe_mode.h"
#include <cstdio>
#include <vector>
namespace ar {
namespace {
std::vector<std::string> split(const std::string &s) {
    std::vector<std::string> v;
    size_t a = 0;
    while (a <= s.size()) {
        const size_t e = s.find(';', a);
        const std::string part = s.substr(a, e == std::string::npos ? std::string::npos : e - a);
        if (!part.empty()) v.push_back(part);
        if (e == std::string::npos) break;
        a = e + 1;
    }
    return v;
}
}
RenderConfig render_config_of(const LookSettings &s) { return RenderConfig{s.msaa, s.foveation, s.render_quality}; }
std::string render_config_key(const RenderConfig &c) {
    char b[96];
    std::snprintf(b, sizeof b, "msaa=%u foveation=%d render_quality=%g", c.msaa, c.foveation ? 1 : 0, (double)c.render_quality);
    return b;
}
RenderConfig render_config_safe() { return RenderConfig{1, false, 0.f}; }
std::string safe_mode_failed_after_launch(const std::string &pending, const std::string &failed) {
    std::vector<std::string> v = split(failed);
    if (!pending.empty()) {
        std::vector<std::string> keep;
        for (const std::string &k : v) if (k != pending) keep.push_back(k);
        keep.push_back(pending);
        v = keep;
    }
    while (v.size() > kMaxFailedConfigs) v.erase(v.begin());
    std::string out;
    for (const std::string &k : v) out += (out.empty() ? "" : ";") + k;
    return out;
}
bool safe_mode_failed_contains(const std::string &failed, const std::string &key) {
    for (const std::string &k : split(failed)) if (k == key) return true;
    return false;
}
LookSettings safe_mode_look(const LookSettings &wanted, const std::string &failed, bool *safe) {
    LookSettings s = wanted;
    const RenderConfig c = render_config_safe();
    const bool use_safe = safe_mode_failed_contains(failed, render_config_key(render_config_of(wanted))) &&
                          render_config_key(render_config_of(wanted)) != render_config_key(c);
    if (use_safe) { s.msaa = (uint8_t)c.msaa; s.foveation = c.foveation; s.render_quality = c.render_quality; }
    if (safe) *safe = use_safe;
    return s;
}
void RenderProbe::set_sink(const Sink &s) { std::lock_guard<std::mutex> g(m_); sink_ = s; }
void RenderProbe::begin(RenderRisk r, const std::string &config) {
    std::lock_guard<std::mutex> g(m_);
    if (!sink_.write) return;
    sink_.write(sink_.ctx, config.c_str());
    owner_ = (int)r;
}
void RenderProbe::end(RenderRisk r) {
    std::lock_guard<std::mutex> g(m_);
    if (owner_ != (int)r) return;
    owner_ = 0;
    if (sink_.clear) sink_.clear(sink_.ctx);
}
void RenderProbe::end_all() {
    std::lock_guard<std::mutex> g(m_);
    if (!owner_) return;
    owner_ = 0;
    if (sink_.clear) sink_.clear(sink_.ctx);
}
bool RenderProbe::pending() const { std::lock_guard<std::mutex> g(m_); return owner_ != 0; }
}
