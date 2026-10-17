#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
extern "C" {
#include "ar_platform.h"
}
#include "render_device.h"
#include "vos_input.h"
#include "vos_placer.h"
namespace ar {
class VosPads;
class VosPlatform {
public:
    struct Config {
        VosPlacer::Config placer; float table_half_x = 0.f, table_half_z = 0.f;
        std::vector<std::array<float, 3>> sim_pinch;
    };
    explicit VosPlatform(const Config &c);
    ~VosPlatform();
    const ar_platform *api() const;
    void attach(void *layer_renderer);
    void detach();
    void *metal_device() const;
    bool external_view(uint32_t i, ExternalView *out) const;
    // The attached layer's layout is layered: the views share one texture, view i in array slice i (the device). The
    // simulator's dedicated layout is not (a texture per view).
    bool layered() const;
    // map: unretained, valid for this frame
    struct RateMap {
        void *map = nullptr;
        uint32_t maps = 0, layers = 0;
        uint32_t screen[2] = {0, 0};
        uint32_t physical[AR_MAX_VIEWS][2] = {};
        uint32_t texture[2] = {0, 0};
    };
    bool rate_map(RateMap *out) const;
    void set_render_quality(float q);
    bool color_wants_linear() const;
    uint32_t color_format() const;
    void encode_present(void *command_buffer);   // after ar_runtime_submit_frame, before commit
    void end_submission();                        // after the command buffer is committed
    void set_pads(VosPads *pads);
    void set_holding(bool on);
    // Main thread. Backgrounded: the layer does not count as running (the game pauses). Closed: the space was
    // dismissed, so a paused layer ends the frame loop instead of waiting to run again (a reopen brings a new layer).
    void set_background(bool on);
    void space_closed();
    bool placed() const;
    void replace();
    bool pad_lost() const;
    void set_replaying(bool on);
    std::string placement_hint() const;
    void push_spatial_event(const SpatialEvent &e);   // main thread (onSpatialEvent); the layer thread drains it in fill
    void set_pointer_claim(PointerClaim claim, bool pose_only = false);
    // Any thread: ARKit hand tracking in its own session (a denial never stops world tracking), started once per
    // process on the first call.
    void start_hand_tracking();
    void set_hand_sink(HandSink sink);
    // Any thread; the sink is called on the render thread.
    void set_table_placed_sink(std::function<void()> sink);
    struct Impl;
private:
    Impl *impl_;
};
}
