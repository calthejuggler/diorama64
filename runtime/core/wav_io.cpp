#include "wav_io.h"
#include <cstdio>
namespace ar {
static void put32(std::FILE *f, uint32_t v) { const uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)}; std::fwrite(b, 1, 4, f); }
static void put16(std::FILE *f, uint16_t v) { const uint8_t b[2] = {uint8_t(v), uint8_t(v >> 8)}; std::fwrite(b, 1, 2, f); }
bool wav_write_s16(const std::string &path, const int16_t *s, size_t frames, uint32_t channels, uint32_t rate) {
    std::FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint32_t data = (uint32_t)(frames * 2 * channels);
    std::fwrite("RIFF", 1, 4, f); put32(f, 36 + data); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); put32(f, 16); put16(f, 1); put16(f, (uint16_t)channels); put32(f, rate); put32(f, rate * 2 * channels);
    put16(f, (uint16_t)(2 * channels)); put16(f, 16);
    std::fwrite("data", 1, 4, f); put32(f, data);
    for (size_t i = 0; i < frames * channels; i++) put16(f, (uint16_t)s[i]);
    const bool wrote = !std::ferror(f);   // a short fwrite sets the error flag; fclose alone may not report it
    return std::fclose(f) == 0 && wrote;
}
bool wav_write_s16_stereo(const std::string &path, const int16_t *s, size_t frames, uint32_t rate) {
    return wav_write_s16(path, s, frames, 2, rate);
}
}
