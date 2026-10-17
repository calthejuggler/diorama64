#pragma once
#ifdef __cplusplus
extern "C" {
#endif
/* Points gArGameHooks at the adapter's hooks. Safe before boot; the game uses them only while gArMode is set. */
void sm64_ar_hooks_install(void);
#ifdef __cplusplus
}
#endif
