#include "pc/controller/controller_api.h"
#include "sm64_host.h"
static sm64_pad sPad;
void ar_controller_set(const sm64_pad *p) { sPad = *p; }
static void c_init(void) {}
/* 0002-Library-seams-and-host-hooks.patch makes the injection exclusive (pad zeroed, physical controllers not read), so the
 * host's pad is the whole input: assign everything, including a neutral stick and no buttons. */
static void c_read(OSContPad *pad) {
    pad->button  = sPad.buttons;
    pad->stick_x = sPad.stick_x;
    pad->stick_y = sPad.stick_y;
}
static u32 c_rawkey(void) { return VK_INVALID; }
struct ControllerAPI gControllerAr = { .vkbase = 0, .init = c_init, .read = c_read, .rawkey = c_rawkey };
