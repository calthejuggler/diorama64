/* Force-included into Lua's loslib.c: os.execute never spawns a shell (system() is __IOS_PROHIBITED on visionOS). */
#pragma once
#include <stdlib.h>
int ar_lua_no_system(const char *cmd);
#define system(cmd) ar_lua_no_system(cmd)
