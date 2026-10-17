#import <GameController/GameController.h>
#include "gc_pad.h"
namespace ar {
bool poll_gamepad(ar_pad *out) {
    GCController *c = GCController.current ?: GCController.controllers.firstObject;
    GCExtendedGamepad *g = c.extendedGamepad;
    ar_pad p{};
    if (g) {
        if (g.buttonA.pressed) p.buttons |= AR_BTN_A;
        if (g.buttonB.pressed) p.buttons |= AR_BTN_B;
        if (g.buttonX.pressed) p.buttons |= AR_BTN_X;
        if (g.buttonY.pressed) p.buttons |= AR_BTN_Y;
        if (g.leftShoulder.pressed) p.buttons |= AR_BTN_LB;
        if (g.rightShoulder.pressed) p.buttons |= AR_BTN_RB;
        if (g.buttonMenu.pressed) p.buttons |= AR_BTN_START;
        if (g.buttonOptions.pressed) p.buttons |= AR_BTN_SELECT;
        if (g.dpad.up.pressed) p.buttons |= AR_BTN_DPAD_UP;
        if (g.dpad.down.pressed) p.buttons |= AR_BTN_DPAD_DOWN;
        if (g.dpad.left.pressed) p.buttons |= AR_BTN_DPAD_LEFT;
        if (g.dpad.right.pressed) p.buttons |= AR_BTN_DPAD_RIGHT;
        p.lx = g.leftThumbstick.xAxis.value; p.ly = g.leftThumbstick.yAxis.value;
        p.rx = g.rightThumbstick.xAxis.value; p.ry = g.rightThumbstick.yAxis.value;
        p.lt = g.leftTrigger.value; p.rt = g.rightTrigger.value;
    }
    *out = p;
    return g != nil;
}
}
