#include "pipeline_warm.h"
#include <set>
namespace ar {
std::vector<DrawPacket> pipeline_siblings(const DrawPacket &d) {
    std::vector<DrawPacket> out;
    if (!d.blend || d.silhouette || d.depth_only || d.table_shadow) return out;
    for (uint8_t tier : {kDepthOnlyHalf, kDepthOnlyFaint}) {
        DrawPacket z = d;
        z.depth_only = tier; z.blend = 0; z.depth_write = 1; z.focus = 0;
        out.push_back(z);
    }
    return out;
}
std::string merge_pipeline_lists(const std::string &a, const std::string &b) {
    std::string out;
    std::set<std::string> seen;
    for (const std::string *list : {&a, &b}) {
        size_t pos = 0;
        while (pos < list->size()) {
            size_t e = list->find('\n', pos);
            if (e == std::string::npos) e = list->size();
            const std::string line = list->substr(pos, e - pos);
            pos = e + 1;
            if (line.find_first_not_of(" \t\r") == std::string::npos || !seen.insert(line).second) continue;
            out += line + "\n";
        }
    }
    return out;
}
}
