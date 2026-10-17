// GameController is read on the main thread only: poll() runs there from a timer (vos_app.mm); the layer thread's
// readers only copy that state. GCVirtualController is unavailable on visionOS.
#pragma once
#include <cstdint>
#include <mutex>
extern "C" {
#include "ar_types.h"
}
#include "vos_state.h"
namespace ar {
class VosPads {
public:
    void poll();
    int get(uint32_t player, ar_pad *out);
    bool connected_now() const;
    bool peek_a() const;
    void hold_until_released(uint32_t mask);
private:
    HeldButtonGate gate_;
    mutable std::mutex m_;
    ar_pad pad_{};
    bool connected_ = false;
};
}
