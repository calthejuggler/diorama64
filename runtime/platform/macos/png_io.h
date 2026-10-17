#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace ar {
bool png_write(const std::string &path, const uint8_t *rgba, uint32_t w, uint32_t h);
bool png_read(const std::string &path, std::vector<uint8_t> &rgba, uint32_t &w, uint32_t &h);
}
