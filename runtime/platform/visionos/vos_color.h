#pragma once
#include <cstdint>
namespace ar {
// True for sRGB formats (the hardware re-encodes) and float formats (the compositor reads them as linear): the resolve
// then linearizes our gamma-space N64 colour (an RGBA16Float drawable looked washed out without it).
bool vos_format_wants_linear(uint32_t mtl_pixel_format);
bool vos_linearize(uint8_t colour, bool drawable_wants_linear);
}
