#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include "vos_place_gesture.h"
#include "vos_placement.h"
#include "vos_planes.h"
namespace ar {
enum class PreviewState { none, seeking, dragging };
struct PlacerPreview {
    PreviewState state = PreviewState::none;
    bool reticle = false, on_plane = false;
    ar_pose pose{{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 1.f}};
    std::vector<PlaneInfo> planes;
};
class VosPlacer {
public:
    struct Config {
        PlaceParams place; std::string game; std::string record_path; bool reset = false;
        bool planes = false;
        double seek_timeout_s = 15.0;   // unused: no timeout placement, the table waits for a pinch or A
    };
    explicit VosPlacer(const Config &c);
    void attach_world(void *world_tracking_provider);
    // Layer thread, every frame; ARKit anchor requests go to a queue.
    void update(const ar_pose &head, bool tracked, double now_s, bool confirm, const std::vector<SpatialEvent> &events);
    PlacerPreview preview() const;
    void set_planes(std::vector<PlaneInfo> planes);
    void planes_unavailable();
    void planes_authorized();
    void request_replace();
    std::string hint() const;
    bool valid() const;
    uint64_t placements() const;
    ar_pose tracking_from_table() const;
    // ARKit callbacks (anchor queue). Public only for the Objective-C blocks in vos_placer.mm.
    void on_anchor(const std::string &uuid, const ar_pose &tracking_from_anchor);
    void on_anchor_added(bool ok, const std::string &uuid, long code, uint64_t placement);
private:
    void place_fresh(const ar_pose &head, const char *source);   // m_ held
    void place_at(const ar_pose &table, const char *source);     // m_ held
    void request_anchor(void *world, const ar_pose &at, uint64_t placement);   // m_ NOT held: ARKit may answer synchronously
    void remove_anchor(void *world, const std::string &uuid);     // m_ NOT held; the replaced record's anchor
    void request_anchor_async(std::unique_lock<std::mutex> &held);   // m_ held; unlocks it: a world anchor for table_
    bool apply_gesture(const GestureResult &r);                     // m_ held; true: placed or committed (an anchor is due)
    void save();
    Config cfg_;
    mutable std::mutex m_;
    bool valid_ = false, waiting_anchor_ = false, anchor_seen_ = false;
    ar_pose table_ = {{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 1.f}};
    PlacementRecord record_;
    std::string stale_anchor_;
    uint64_t frames_ = 0, tracked_frames_ = 0;
    uint64_t placements_ = 0;
    void *world_ = nullptr;
    std::vector<PlaneInfo> planes_;
    bool seeking_ = false;
    bool planes_ready_ = false;
    GestureConfig gesture_cfg_;
    PlaceGesture gesture_;
    Reticle reticle_;
    ar_pose last_head_{{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 1.f}};
    double last_probe_s_ = -1.0;
};
}
