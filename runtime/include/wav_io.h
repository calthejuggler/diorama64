#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace ar {
bool wav_write_s16_stereo(const std::string &path, const int16_t *stereo, size_t frames, uint32_t rate_hz);
bool wav_write_s16(const std::string &path, const int16_t *interleaved, size_t frames, uint32_t channels, uint32_t rate_hz);
}
