#include "vos_color.h"
namespace ar {
bool vos_format_wants_linear(uint32_t f) {
    switch (f) {
        case 71: case 81: case 553: case 555:   // RGBA8Unorm_sRGB, BGRA8Unorm_sRGB, BGRA10_XR_sRGB, BGR10_XR_sRGB
        case 115: case 125:                     // RGBA16Float, RGBA32Float
            return true;
        default: return false;
    }
}
bool vos_linearize(uint8_t colour, bool wants) { return colour == 1 ? true : colour == 2 ? false : wants; }
}
