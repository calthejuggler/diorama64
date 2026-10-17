/* No-ROM test game: a 40 cm cube on the ground; the left stick slides it. */
#pragma once
#include "ar_adapter.h"
#ifdef __cplusplus
extern "C" {
#endif
const ar_adapter *cube_adapter(void);
ar_player_input cube_last_input(void);
uint32_t cube_ticks(void);
void cube_set_publish(int on);
#define CUBE_SPEED_UNITS 40.f
void cube_position(float out[2]);
#ifdef __cplusplus
}
#endif
