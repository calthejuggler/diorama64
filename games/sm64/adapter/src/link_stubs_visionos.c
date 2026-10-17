/* Stand-ins for upstream modules excluded from the visionOS build (games/sm64/CMakeLists.txt). */
#include <stdbool.h>
#include "pc/controller/controller_api.h"
#include "pc/crash_handler.h"
bool gUpdateMessage = false;
void check_for_updates(void) {}
void show_update_popup(void) {}
void crash_handler_init(void) {}
/* crash_handler.c data the rest of the game uses: packet_spawn_objects.c writes gLastRemoteBhv; configfile.c saves
 * gPcDebug.tags/bhvOffset and smlua_hooks.c reads bhvOffset. Same initial values as crash_handler.c:693 so config
 * files match the macOS build. */
char gLastRemoteBhv[256] = "";
struct PcDebug gPcDebug = {
    .tags = {
        0x0000000000000000, 0x000000000000FFFF, 0x2D1D50FB02617949, 0x8AEB7180FAE739EB, 0x0CDB1A233CC71057,
        0x53D5D9880C8B278E, 0xE8E307BE5802542E, 0x8A3ACC4FDB4FFE45, 0x09046C2BA3C5000D, 0xF027964ADE989C29,
        0x076CF19655C70007, 0x440C28A5CC404F11, 0xE9A402C28144FD8B, 0x9A2269E87B26BE68, 0x0E76DE227D813019,
        0x12ABA8362D430002, 0x0BF8F9C076430007, 0x0BFA2492BA430011,
    },
    .id = DEFAULT_ID,
    .bhvOffset = 0,
    .debugId = 0x4BE2,
    .lastModRun = NULL,
};
static void c_init(void) {}
static void c_read(OSContPad *pad) { (void)pad; }
static u32 c_rawkey(void) { return VK_INVALID; }
struct ControllerAPI controller_sdl = { 0x1000, c_init, c_read, c_rawkey, NULL, NULL, NULL, NULL };
