// Some layer configurations abort the process inside Metal or CompositorServices (an assert, not an error): a layered
// multisample pipeline, a rate map, a max render quality. One still pending at the next launch crashed it and is not
// used again.
#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include "look.h"
namespace ar {
struct RenderConfig { uint32_t msaa = 1; bool foveation = false; float render_quality = 0.f; };
RenderConfig render_config_of(const LookSettings &s);
std::string render_config_key(const RenderConfig &c);
RenderConfig render_config_safe();
constexpr size_t kMaxFailedConfigs = 8;
std::string safe_mode_failed_after_launch(const std::string &pending, const std::string &failed);
bool safe_mode_failed_contains(const std::string &failed, const std::string &key);
LookSettings safe_mode_look(const LookSettings &wanted, const std::string &failed, bool *safe);
enum class RenderRisk : int {
    layer_create = 1,
    render_quality,
    pipelines,
    first_frame,
};
// begin() stores the configuration as pending (synchronously, flushed: the process may abort right after) and makes
// that risk its owner; end() clears it only while that risk still owns it.
class RenderProbe {
public:
    struct Sink { void (*write)(void *ctx, const char *config); void (*clear)(void *ctx); void *ctx; };
    void set_sink(const Sink &s);
    void begin(RenderRisk r, const std::string &config);
    void end(RenderRisk r);
    void end_all();
    bool pending() const;
private:
    mutable std::mutex m_;
    Sink sink_{nullptr, nullptr, nullptr};
    int owner_ = 0;
};
class RenderProbeScope {
public:
    RenderProbeScope(RenderProbe *p, RenderRisk r, const std::string &config) : p_(p), r_(r) { if (p_) p_->begin(r_, config); }
    ~RenderProbeScope() { if (p_) p_->end(r_); }
    RenderProbeScope(const RenderProbeScope &) = delete;
    RenderProbeScope &operator=(const RenderProbeScope &) = delete;
private:
    RenderProbe *p_;
    RenderRisk r_;
};
}
