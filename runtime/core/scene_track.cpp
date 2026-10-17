#include "ar_scene_track.h"
#include <cstdio>
#include <cstring>
#include <string>
extern "C" size_t ar_scene_track_line(const ar_audio_tick_meta *m, uint64_t tick, uint64_t origin, char *out, size_t cap) {
    std::string s;
    char b[160];
    std::snprintf(b, sizeof b, "{\"tick\":%llu,\"frame\":%llu,\"frames\":%u,\"head\":[", (unsigned long long)tick,
                  (unsigned long long)(m->first_frame - origin), m->frames);
    s += b;
    const float h[7] = {m->listener.pos[0], m->listener.pos[1], m->listener.pos[2], m->listener.quat[0], m->listener.quat[1],
                        m->listener.quat[2], m->listener.quat[3]};
    for (int k = 0; k < 7; k++) { std::snprintf(b, sizeof b, "%s%.5f", k ? "," : "", h[k]); s += b; }
    s += "],\"bed\":[";
    const float bed[6] = {m->bed_anchor[0], m->bed_anchor[1], m->bed_anchor[2], m->bed_half_extent[0], m->bed_half_extent[1],
                          m->bed_half_extent[2]};
    for (int k = 0; k < 6; k++) { std::snprintf(b, sizeof b, "%s%.5f", k ? "," : "", bed[k]); s += b; }
    std::snprintf(b, sizeof b, "],\"dropped\":%u,\"lanes\":[", m->dropped);
    s += b;
    bool first = true;
    for (uint32_t l = 0; l < AR_AUDIO_MAX_SOURCES; l++) {
        const ar_audio_source &src = m->lane[l];
        if (!(src.flags & AR_SRC_ACTIVE)) continue;
        std::snprintf(b, sizeof b, "%s[%u,%u,%u,%.5f,%.5f,%.5f,%.5f]", first ? "" : ",", l, src.id, src.flags, src.pos[0], src.pos[1], src.pos[2],
                      src.gain);
        s += b;
        first = false;
    }
    s += "]}";
    if (s.size() + 1 > cap) { if (cap) out[0] = '\0'; return 0; }
    std::memcpy(out, s.c_str(), s.size() + 1);
    return s.size();
}
