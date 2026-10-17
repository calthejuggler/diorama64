#pragma once
#include <string>
extern "C" {
#include "ar_math.h"
}
namespace ar {
struct PlaceParams { float forward_m = 1.2f, below_m = 0.5f, yaw_deg = 0.f; bool desk_height = true; };
constexpr float kDeskHeightM = 0.74f;            // EN 527 desk height: the fallback table top above the floor
constexpr float kStandingEyeHeightM = 1.60f;
ar_pose vos_table_from_head(const ar_pose &tracking_from_head, const PlaceParams &p);
struct PlacementRecord {
    std::string anchor = "fixed";
    std::string game;
    float yaw_deg = 0.f;
    ar_pose tracking_from_table = {{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 1.f}};
};
std::string placement_to_json(const PlacementRecord &r);
// Strict: version 1, every key present, finite numbers, |pos| < 100 m, quaternion of unit length (±1e-3; normalized).
// On failure *out is untouched and *err says why.
bool placement_from_json(const std::string &text, PlacementRecord *out, std::string *err);
bool placement_save(const std::string &path, const PlacementRecord &r, std::string *err);
bool placement_load(const std::string &path, PlacementRecord *out, std::string *err);
bool placement_anchor_applies(const PlacementRecord &r, const std::string &uuid);
void placement_forget(PlacementRecord *r, std::string *stale);
std::string placement_stale_anchor(const PlacementRecord &replaced);
bool placement_reset(const std::string &path, std::string *stale_anchor, std::string *err);
}
