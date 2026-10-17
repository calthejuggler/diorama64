#include <stdbool.h>
#include <string.h>
#include "pc/network/network.h"

static bool  n_init(enum NetworkType t, bool r) { (void)t; (void)r; return true; }
static s64   n_get_id(u8 i) { (void)i; return 0; }
static char *n_get_id_str(u8 i) { (void)i; static char s[] = "0"; return s; }
static void  n_save_id(u8 i, s64 id) { (void)i; (void)id; }
static void  n_clear_id(u8 i) { (void)i; }
static void *n_dup_addr(u8 i) { (void)i; return NULL; }
static bool  n_match_addr(void *a, void *b) { return a == b; }
static void  n_update(void) {}
static int   n_send(u8 i, void *a, u8 *d, u16 l) { (void)i; (void)a; (void)d; (void)l; return 0; }
static void  n_lobby(char *d, u32 l) { if (l) d[0] = '\0'; }
static void  n_shutdown(bool r) { (void)r; }

struct NetworkSystem gNetworkSystemArNull = {
    .initialize = n_init, .get_id = n_get_id, .get_id_str = n_get_id_str, .save_id = n_save_id,
    .clear_id = n_clear_id, .dup_addr = n_dup_addr, .match_addr = n_match_addr, .update = n_update,
    .send = n_send, .get_lobby_id = n_lobby, .get_lobby_secret = n_lobby, .shutdown = n_shutdown,
    .requireServerBroadcast = false, .name = "AR offline",
};
