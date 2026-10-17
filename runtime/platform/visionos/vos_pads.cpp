#include "vos_pads.h"
#include "gc_pad.h"
namespace ar {
void VosPads::poll() {
    ar_pad p{};
    const bool c = poll_gamepad(&p);
    std::lock_guard<std::mutex> g(m_);
    pad_ = p;
    connected_ = c;
}
int VosPads::get(uint32_t player, ar_pad *out) {
    *out = ar_pad{};
    if (player != 0) return 0;
    std::lock_guard<std::mutex> g(m_);
    *out = pad_;
    gate_.filter(out);
    return connected_ ? 1 : 0;
}
bool VosPads::connected_now() const { std::lock_guard<std::mutex> g(m_); return connected_; }
void VosPads::hold_until_released(uint32_t mask) { std::lock_guard<std::mutex> g(m_); gate_.arm(mask); }
bool VosPads::peek_a() const { std::lock_guard<std::mutex> g(m_); return connected_ && (pad_.buttons & AR_BTN_A) != 0; }
}
