#pragma once
#include "ar_types.h"
#include "sm64_host.h"
#ifdef __cplusplus
extern "C" {
#endif
/* N64 controller bits; sm64_host.c static-asserts them against include/PR/os_cont.h. */
enum { SM64_N64_A = 0x8000, SM64_N64_B = 0x4000, SM64_N64_Z = 0x2000, SM64_N64_START = 0x1000,
       SM64_N64_L = 0x0020, SM64_N64_R = 0x0010, SM64_N64_CU = 0x0008, SM64_N64_CD = 0x0004,
       SM64_N64_CL = 0x0002, SM64_N64_CR = 0x0001 };
sm64_pad sm64_pad_from_ar(const ar_pad *p);
/* AR: C buttons, R and L are masked (no Lakitu camera, no first-person look; L reloads mods in pause). */
sm64_pad sm64_pad_mask_ar(sm64_pad p);
#ifdef __cplusplus
}
#endif
