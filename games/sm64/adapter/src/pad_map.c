#include "sm64_pad_map.h"
#include <math.h>
static int8_t axis(float v) {
    if (!(v == v)) v = 0.f;
    if (v > 1.f) v = 1.f;
    if (v < -1.f) v = -1.f;
    return (int8_t)lrintf(v * 80.f);
}
sm64_pad sm64_pad_from_ar(const ar_pad *p) {
    sm64_pad o = { 0, 0, 0 };
    uint32_t b = p->buttons;
    if (b & AR_BTN_A) o.buttons |= SM64_N64_A;
    if (b & (AR_BTN_B | AR_BTN_X)) o.buttons |= SM64_N64_B;
    if ((b & AR_BTN_LB) || p->lt > 0.5f) o.buttons |= SM64_N64_Z;
    if ((b & AR_BTN_RB) || p->rt > 0.5f) o.buttons |= SM64_N64_R;
    if (b & AR_BTN_START) o.buttons |= SM64_N64_START;
    if (b & AR_BTN_SELECT) o.buttons |= SM64_N64_L;
    if (p->ry > 0.5f) o.buttons |= SM64_N64_CU;
    if (p->ry < -0.5f) o.buttons |= SM64_N64_CD;
    if (p->rx < -0.5f) o.buttons |= SM64_N64_CL;
    if (p->rx > 0.5f) o.buttons |= SM64_N64_CR;
    o.stick_x = axis(p->lx);
    o.stick_y = axis(p->ly);
    return o;
}
sm64_pad sm64_pad_mask_ar(sm64_pad p) {
    p.buttons &= (uint16_t)~(SM64_N64_CU | SM64_N64_CD | SM64_N64_CL | SM64_N64_CR | SM64_N64_R | SM64_N64_L);
    return p;
}
