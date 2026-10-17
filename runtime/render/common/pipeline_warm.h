#pragma once
#include <string>
#include <vector>
#include "render_device.h"
namespace ar {
// Silhouette copies need no sibling: their pipeline does not depend on the combiner.
std::vector<DrawPacket> pipeline_siblings(const DrawPacket &d);
std::string merge_pipeline_lists(const std::string &a, const std::string &b);
}
