#pragma once
#include <vector>
#include "render_device.h"
namespace ar { void build_packets(const fc_draw *draws, uint32_t n, std::vector<DrawPacket> &out); }
