// wait_frame, get_table, session_state and end_frame run on the layer thread; get_pad is called by the game tick and
// reads the pad the main thread polled (VosPads). Plane updates arrive on their own serial queue.
#import <ARKit/ARKit.h>
#import <CompositorServices/CompositorServices.h>
#import <Metal/Metal.h>
#include "vos_platform.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <uuid/uuid.h>
#include <vector>
#include "vos_contract.h"
#include "vos_log.h"
#include "vos_pads.h"
#include "vos_planes.h"
#include "vos_state.h"
#include "vos_color.h"
extern "C" {
#include "ar_math.h"
}
namespace ar {
namespace {
ar_mat4 from_simd(simd_float4x4 s) { ar_mat4 m; std::memcpy(m.m, &s, sizeof m.m); return m; }
std::string plane_uuid(ar_plane_anchor_t a) { uuid_t u; ar_plane_anchor_get_identifier(a, u); char s[37]; uuid_unparse_upper(u, s); return s; }
PlaneInfo plane_info(ar_plane_anchor_t a) {
    ar_plane_extent_t e = ar_plane_geometry_get_plane_extent(ar_plane_anchor_get_geometry(a));
    const simd_float4x4 m = simd_mul(ar_plane_anchor_get_origin_from_anchor_transform(a),
                                     ar_plane_extent_get_plane_anchor_from_plane_extent_transform(e));
    float f[16];
    std::memcpy(f, &m, sizeof f);
    return plane_info_from_matrix(f, ar_plane_extent_get_width(e), ar_plane_extent_get_height(e),
                                  ar_plane_anchor_get_surface_classification(a) == ar_surface_classification_table,
                                  ar_plane_anchor_get_surface_classification(a) == ar_surface_classification_floor);
}
}

struct VosPlatform::Impl {
    explicit Impl(const VosPlatform::Config &c) : cfg(c), placer(c.placer), sim_pinch(c.sim_pinch) {}
    VosPlatform::Config cfg;
    VosPlacer placer;
    VosPads *pads = nullptr;
    ar_platform api{};
    cp_layer_renderer_t layer = nil;
    ar_session_t session = nil;
    ar_world_tracking_provider_t world = nil;
    // Plane detection runs in its own session, started once world sensing is allowed: a denial must not stop world
    // tracking, and the placer then places from the head instead of waiting for planes.
    ar_session_t plane_session = nil;
    ar_plane_detection_provider_t planes = nil;
    std::map<std::string, PlaneInfo> plane_map;   // the plane queue only
    PadWatch pads_watch;                          // layer thread
    std::atomic<bool> pad_lost{false};
    bool replaying = false;
    bool prev_a = false;
    bool was_placed = false;
    bool planes_started = false;
    cp_frame_t frame = nullptr;
    cp_drawable_t drawable = nullptr;
    ar_device_anchor_t anchor = nil;
    bool anchor_ok = false, anchor_tracked = false;
    ar_pose head = {{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 1.f}};
    bool ever_tracked = false;
    int64_t last_tracked_ns = 0, display_ns = 0;
    float display_hz = 0.f;
    uint64_t frames_since_attach = 0;
    std::string last_contract;
    size_t contract_views = 0, contract_textures = 0;
    std::atomic<bool> running{false}, holding{false}, background{false}, closed{false};
    SpatialEventQueue input;
    std::vector<SpatialEvent> events;
    std::vector<std::array<float, 3>> sim_pinch;
    double first_tracked_s = -1.0;
    size_t scripted_next = 0;
    std::deque<SpatialEvent> pending_scripted;
    uint64_t input_logged = 0;
    PointerRouter router;
    PointerClaim claim;                               // set_pointer_claim (any thread, under claim_m); empty = none
    bool claim_pose_only = false;
    HandSink hand_sink;
    std::function<void()> table_placed_sink;
    TablePlacedGate table_gate;
    std::mutex claim_m;
    // hand tracking, in its own session. hands is written once (authorization queue) before hands_running.
    std::atomic<bool> hands_started{false}, hands_running{false};
    ar_session_t hand_session = nil;
    ar_hand_tracking_provider_t hands = nil;
    ar_hand_anchor_t hand_left = nil, hand_right = nil;   // layer thread
    uint64_t hand_frames = 0;
    bool hands_fed = false;
    void start_hands();
    void take_hands(double t);
    GestureContinuity continuity;
    bool discard_input = true;
    bool logged_extra = false, logged_fallback = false, logged_projection = false, logged_anchor_failure = false;

    int wait_frame(ar_frame_info *out);
    void fill(ar_frame_info *out, double display_s);
    void start_planes();
    void take_events(double t);
    // environment light estimation, in its own session (as planes: a denial must never stop world tracking).
    ar_session_t light_session = nil;
    ar_environment_light_estimation_provider_t light = nil;
    std::mutex light_m;                                   // the light queue writes, the layer thread reads
    ar_environment_probe_anchor_t light_latest = nil;     // strong: the texture lives as long as its anchor (ARKit environment_light_estimation.h:67)
    uint32_t light_generation = 0;
    struct Probe { ar_environment_probe_anchor_t anchor; uint64_t seq; };
    std::map<std::string, Probe> light_probes;
    ar_environment_probe_anchor_t light_held = nil;       // layer thread: keeps this frame's texture alive
    id<MTLTexture> light_last_tex = nil;
    ar_environment light_last_env{};
    uint64_t light_updates = 0;
    double light_last_s = 0.0;
    bool light_started = false;
    void start_light();
    void take_probe(ar_environment_probe_anchor_t a, bool removed, size_t added, size_t updated);
};

namespace {
VosPlatform::Impl *impl_of(void *u) { return static_cast<VosPlatform::Impl *>(u); }
int p_wait(void *u, ar_frame_info *o) { return impl_of(u)->wait_frame(o); }
int p_table(void *u, ar_table *o) {
    VosPlatform::Impl *im = impl_of(u);
    if (!im->placer.valid()) return 0;
    if (im->table_gate.step(im->placer.placements(), im->anchor_tracked)) {
        std::function<void()> sink;
        { std::lock_guard<std::mutex> g(im->claim_m); sink = im->table_placed_sink; }
        if (sink) sink();
    }
    o->tracking_from_table = im->placer.tracking_from_table();
    o->half_extent[0] = im->cfg.table_half_x;
    o->half_extent[1] = im->cfg.table_half_z;
    return 1;
}
int p_pad(void *u, uint32_t player, ar_pad *o) {
    VosPlatform::Impl *im = impl_of(u);
    if (!im->pads) { *o = ar_pad{}; return 0; }
    return im->pads->get(player, o);
}
ar_session_state p_state(void *u) {
    VosPlatform::Impl *im = impl_of(u);
    const double untracked = im->ever_tracked && im->display_ns > im->last_tracked_ns ? (im->display_ns - im->last_tracked_ns) * 1e-9 : 0.0;
    return vos_session_state(im->running && !im->background, im->placer.valid(), im->holding, untracked, im->frames_since_attach,
                             im->pads_watch.lost());
}
int p_preview(void *u, ar_preview *o) {
    VosPlatform::Impl *im = impl_of(u);
    PlacerPreview pv = im->placer.preview();
    *o = ar_preview{};
    if (pv.state == PreviewState::dragging) {
        o->state = AR_PREVIEW_DRAGGING;
        o->tracking_from_preview = pv.pose;
        return 1;
    }
    if (pv.state != PreviewState::seeking || (!pv.reticle && pv.planes.empty())) return 1;
    o->state = AR_PREVIEW_SEEKING;
    o->outlines_only = pv.reticle ? 0 : 1;
    o->on_plane = pv.on_plane ? 1 : 0;
    o->tracking_from_preview = pv.pose;
    const float *h = im->head.pos;
    auto dist2 = [h](const PlaneInfo &p) {
        float s = 0.f;
        for (int k = 0; k < 3; k++) s += (p.center[k] - h[k]) * (p.center[k] - h[k]);
        return s;
    };
    std::sort(pv.planes.begin(), pv.planes.end(), [&](const PlaneInfo &a, const PlaneInfo &b) { return dist2(a) < dist2(b); });
    const size_t n = std::min(pv.planes.size(), (size_t)AR_PREVIEW_MAX_OUTLINES);
    static const float kSign[4][2] = {{-1.f, -1.f}, {1.f, -1.f}, {1.f, 1.f}, {-1.f, 1.f}};
    for (size_t i = 0; i < n; i++) {
        const PlaneInfo &p = pv.planes[i];
        for (int c = 0; c < 4; c++)
            for (int k = 0; k < 3; k++)
                o->outlines[i][c][k] = p.center[k] + kSign[c][0] * p.half_u * p.u[k] + kSign[c][1] * p.half_v * p.v[k];
    }
    o->outline_count = (uint32_t)n;
    return 1;
}
int p_env(void *u, ar_environment *o) {
    VosPlatform::Impl *im = impl_of(u);
    ar_environment_probe_anchor_t a;
    uint32_t gen;
    { std::lock_guard<std::mutex> g(im->light_m); a = im->light_latest; gen = im->light_generation; }
    im->light_held = a;                                   // retained until the next call: frames being encoded keep it
    id<MTLTexture> t = a ? ar_environment_probe_anchor_get_environment_texture(a) : nil;
    if (!t) {
        if (!im->light_last_tex) return 0;
        *o = im->light_last_env;
        o->cube_texture = (__bridge void *)im->light_last_tex;
        return 1;
    }
    std::memset(o, 0, sizeof *o);
    o->cube_texture = (__bridge void *)t;
    o->camera_scale_reference = ar_environment_probe_anchor_get_camera_scale_reference(a);
    o->generation = gen;
    o->tracking_from_probe = ar_pose_from_mat4(from_simd(ar_environment_probe_anchor_get_origin_from_anchor_transform(a)));
    o->timestamp_ns = (int64_t)llround(ar_environment_probe_anchor_get_timestamp(a) * 1e9);
    im->light_last_tex = t;
    im->light_last_env = *o;
    return 1;
}
void p_end(void *u, const ar_submit_info *) {
    // CS/drawable.h: hand the compositor the device anchor whose pose built this frame's matrices. An untracked anchor did
// not build them (the last tracked head did), so it is not handed over.
    VosPlatform::Impl *im = impl_of(u);
    if (im->drawable && im->anchor_tracked) cp_drawable_set_device_anchor(im->drawable, im->anchor);
}
}

namespace {
constexpr double kPausedPollS = 0.01;
bool layer_is_paused(void *layer) { return cp_layer_renderer_get_state((__bridge cp_layer_renderer_t)layer) == cp_layer_renderer_state_paused; }
}

int VosPlatform::Impl::wait_frame(ar_frame_info *out) {
    for (;;) {
        if (!layer) return 0;
        const cp_layer_renderer_state st = cp_layer_renderer_get_state(layer);
        if (st == cp_layer_renderer_state_invalidated) { running = false; return 0; }
        if (st == cp_layer_renderer_state_paused) {
            running = false;
            vos_log("AR_LAYER paused");
            // Not cp_layer_renderer_wait_until_running: the space can close while we wait (vos_state.h).
            if (!vos_wait_while_paused(layer_is_paused, (__bridge void *)layer, closed, kPausedPollS)) {
                vos_log("AR_LAYER paused after close: leaving the frame loop");
                return 0;
            }
            vos_log("AR_LAYER running");
            frames_since_attach = 0;
            discard_input = true;
            continue;
        }
        running = true;
        cp_frame_t f = cp_layer_renderer_query_next_frame(layer);
        if (!f) continue;
        cp_frame_timing_t timing = cp_frame_predict_timing(f);
        if (!timing) continue;                           // as Apple's sample loop: no timing, no frame to render
        cp_frame_start_update(f);
        cp_frame_end_update(f);
        cp_time_wait_until(cp_frame_timing_get_optimal_input_time(timing));
        cp_frame_start_submission(f);
        cp_drawable_array_t ds = cp_frame_query_drawables(f);
        const size_t n = ds ? cp_drawable_array_get_count(ds) : 0;
        if (n == 0) continue;   // CS/frame.h: no drawables = the frame was cancelled (the layer is pausing or going away);
                                // it is invalid now, so it is neither rendered nor ended. The state check above follows.
        cp_drawable_t d = nullptr;
        for (size_t i = 0; i < n && !d; i++) {
            cp_drawable_t c = cp_drawable_array_get_drawable(ds, i);
            if (cp_drawable_get_target(c) == cp_drawable_target_built_in) d = c;
        }
        if (n > 1 && !logged_extra) { vos_log("AR_DRAWABLES count=%zu: only the built-in one is rendered", n); logged_extra = true; }
        frame = f;
        drawable = d;
        double display_s;
        if (d) {
            frames_since_attach++;
            const size_t vc = cp_drawable_get_view_count(d), tc = cp_drawable_get_texture_count(d);
            if (frames_since_attach == 1 || vc != contract_views || tc != contract_textures) {
                contract_views = vc;
                contract_textures = tc;
                const std::string c = vos_contract_string(layer, d);
                if (c != last_contract) { vos_log("AR_CONTRACT %s", c.c_str()); last_contract = c; }
            }
            display_s = cp_time_to_cf_time_interval(cp_frame_timing_get_presentation_time(cp_drawable_get_frame_timing(d)));
        } else {
            display_s = cp_time_to_cf_time_interval(cp_frame_timing_get_presentation_time(timing));
        }
        fill(out, display_s);
        return 1;
    }
}

void VosPlatform::Impl::fill(ar_frame_info *out, double t) {
    std::memset(out, 0, sizeof *out);
    const int64_t ns = (int64_t)llround(t * 1e9);
    if (display_ns > 0 && ns > display_ns && ns - display_ns < 100000000) {
        const float hz = 1e9f / (float)(ns - display_ns);
        display_hz = display_hz > 0.f ? 0.9f * display_hz + 0.1f * hz : hz;
    }
    display_ns = ns;
    anchor = ar_device_anchor_create();
    const ar_device_anchor_query_status_t q = world ? ar_world_tracking_provider_query_device_anchor_at_timestamp(world, t, anchor)
                                                  : ar_device_anchor_query_status_failure;
    anchor_ok = q == ar_device_anchor_query_status_success;
    if (!anchor_ok && !logged_anchor_failure) {
        vos_log("AR_ARKIT device anchor query failed (status=%d world=%d)", (int)q, world ? 1 : 0);
        logged_anchor_failure = true;
    }
    const bool tracked = anchor_ok && ar_device_anchor_is_tracked(anchor);
    anchor_tracked = tracked;
    if (tracked) {
        head = ar_pose_from_mat4(from_simd(ar_device_anchor_get_origin_from_anchor_transform(anchor)));
        ever_tracked = true;
        last_tracked_ns = ns;
    }
    take_events(t);
    {
        PointerClaim c;
        bool pose_only;
        { std::lock_guard<std::mutex> g(claim_m); c = claim; pose_only = claim_pose_only; }
        if (c) {   // also before placement, so a pinch begun then is remembered as the placer's for good
            size_t edges = 0;
            const size_t n = router.route(&events, placer.valid(), placer.tracking_from_table(), head, c, &edges, pose_only);
            if (edges) vos_log("AR_POINTER routed=%zu", n);
        }
    }
    take_hands(t);
    bool confirm = false;
    if (pads) {
        if (!replaying) {
            pads_watch.update(pads->connected_now(), t);
            if (pads_watch.lost() != pad_lost) {
                pad_lost = pads_watch.lost();
                vos_log("AR_PAD %s", pad_lost ? "lost" : "back");
            }
        }
        const bool a = pads->peek_a();
        confirm = a && !prev_a;
        prev_a = a;
    }
    placer.update(head, tracked, t, confirm, events);
    const bool placed = placer.valid();
    // Just placed (by A, a pinch, or a restore): an A still held must not reach the game as a jump.
    if (placed && !was_placed && pads) pads->hold_until_released(AR_BTN_A);
    was_placed = placed;
    out->display_time_ns = ns;
    out->head_tracked = tracked ? 1 : 0;
    out->display_hz = display_hz;
    out->tracking_from_head = head;
    if (!drawable) return;
    // Near plane: the compositor's own (simulator 0.1 m). Far: infinite, so the projection is exactly the runtime's.
    const simd_float2 dr = cp_drawable_get_depth_range(drawable);   // x = far, y = near
    const float near_m = std::isfinite(dr.y) && dr.y > 0.f ? dr.y : 0.1f;
    if (!std::isinf(dr.x)) cp_drawable_set_depth_range(drawable, simd_make_float2(INFINITY, near_m));
    const ar_mat4 tracking_from_head = ar_mat4_from_pose(head);
    out->view_count = (uint32_t)std::min<size_t>(cp_drawable_get_view_count(drawable), AR_MAX_VIEWS);
    for (uint32_t v = 0; v < out->view_count; v++) {
        cp_view_t view = cp_drawable_get_view(drawable, v);
        out->views[v].tracking_from_eye = ar_pose_from_mat4(ar_mat4_mul(tracking_from_head, from_simd(cp_view_get_transform(view))));
        const simd_float4x4 P = cp_drawable_compute_projection(drawable, cp_axis_direction_convention_right_up_back, v);
        const float *m = reinterpret_cast<const float *>(&P);
        float n = near_m;
        const bool recovered = ar_fov_from_projection(m, &out->views[v].fov, &n) != 0;
        if (!recovered) {
            out->views[v].fov = ar_fov{(m[8] - 1.f) / m[0], (m[8] + 1.f) / m[0], (m[9] + 1.f) / m[5], (m[9] - 1.f) / m[5]};
            n = near_m;
            if (!logged_fallback) {
                vos_log("AR_ERROR projection is not infinite reverse-Z: m=[%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g]", (double)m[0],
                        (double)m[1], (double)m[2], (double)m[3], (double)m[4], (double)m[5], (double)m[6], (double)m[7], (double)m[8],
                        (double)m[9], (double)m[10], (double)m[11], (double)m[12], (double)m[13], (double)m[14], (double)m[15]);
                logged_fallback = true;
            }
        }
        if (v == 0 && !logged_projection) {
            const ar_fov &f = out->views[v].fov;
            vos_log("AR_PROJECTION view=0 left=%.4f right=%.4f up=%.4f down=%.4f near=%.4f recovered=%d", (double)f.left,
                    (double)f.right, (double)f.up, (double)f.down, (double)n, (int)recovered);
            logged_projection = true;
        }
        out->views[v].near_m = n;
        const MTLViewport vp = cp_view_texture_map_get_viewport(cp_view_get_view_texture_map(view));
        out->views[v].width = (uint32_t)vp.width;
        out->views[v].height = (uint32_t)vp.height;
    }
    out->should_render = 1;
}

void VosPlatform::Impl::take_events(double t) {
    events.clear();
    input.drain(&events);
    const GestureContinuity::Result cr = continuity.filter(&events, input.dropped(), discard_input);
    if (cr.discarded || cr.cancelled)
        vos_log("AR_INPUT %s: discarded=%zu cancelled=%zu dropped=%llu", discard_input ? "stale after a pause" : "events dropped",
                cr.discarded, cr.cancelled, (unsigned long long)input.dropped());
    discard_input = false;
    const size_t real = events.size();
    if (anchor_tracked && first_tracked_s < 0.0) first_tracked_s = t;
    while (first_tracked_s >= 0.0 && scripted_next < sim_pinch.size() && t >= first_tracked_s + sim_pinch[scripted_next][0]) {
        const std::array<float, 3> &s = sim_pinch[scripted_next++];
        for (const SpatialEvent &e : scripted_pinch(t, head, s[1], s[2])) pending_scripted.push_back(e);
        vos_log("AR_INPUT scripted pinch at=%.1f dx=%.3f dz=%.3f", (double)s[0], (double)s[1], (double)s[2]);
    }
    while (!pending_scripted.empty() && t >= pending_scripted.front().t_s) {
        events.push_back(pending_scripted.front());
        pending_scripted.pop_front();
    }
    for (size_t i = 0; i < events.size(); i++, input_logged++) {
        const SpatialEvent &e = events[i];
        if (input_logged >= 20 && input_logged % 30 != 0 && e.phase == SpatialPhase::active) continue;
        vos_log("AR_INPUT kind=%d phase=%d id=%llx ray=%d o=%.3f,%.3f,%.3f d=%.3f,%.3f,%.3f pose=%d p=%.3f,%.3f,%.3f "
                "head=%.3f,%.3f,%.3f scripted=%d dropped=%llu",
                (int)e.kind, (int)e.phase, (unsigned long long)e.id, e.has_ray ? 1 : 0, (double)e.ray_o[0], (double)e.ray_o[1],
                (double)e.ray_o[2], (double)e.ray_d[0], (double)e.ray_d[1], (double)e.ray_d[2], e.has_pose ? 1 : 0,
                (double)e.pose_pos[0], (double)e.pose_pos[1], (double)e.pose_pos[2], (double)head.pos[0], (double)head.pos[1],
                (double)head.pos[2], i >= real ? 1 : 0, (unsigned long long)input.dropped());
    }
}

VosPlatform::VosPlatform(const Config &c) : impl_(new Impl(c)) {
    ar_platform &a = impl_->api;
    a.abi_version = AR_PLATFORM_ABI_VERSION;
    a.user = impl_;
    a.wait_frame = p_wait; a.get_table = p_table; a.get_pad = p_pad; a.session_state = p_state; a.end_frame = p_end;
    a.get_environment = p_env;
    a.get_preview = p_preview;
}
VosPlatform::~VosPlatform() { delete impl_; }
const ar_platform *VosPlatform::api() const { return &impl_->api; }
void VosPlatform::attach(void *layer) {
    impl_->layer = (__bridge cp_layer_renderer_t)layer;
    impl_->frames_since_attach = 0;
    impl_->discard_input = true;
    impl_->last_contract.clear();
    impl_->logged_projection = false;
    impl_->closed = false;
    if (!impl_->session && ar_world_tracking_provider_is_supported()) {
        impl_->world = ar_world_tracking_provider_create(ar_world_tracking_configuration_create());
        impl_->placer.attach_world((__bridge void *)impl_->world);   // anchor update handler first
        impl_->session = ar_session_create();
        ar_session_run(impl_->session, ar_data_providers_create_with_data_providers(impl_->world, nil));
    }
    if (impl_->cfg.placer.planes && !impl_->planes_started) {
        impl_->planes_started = true;
        if (ar_plane_detection_provider_is_supported()) {
            impl_->start_planes();
        } else {
            vos_log("AR_PLANES unsupported here: searching with no planes (--sim-plane-seek)");
            impl_->placer.planes_authorized();
        }
    }
    if (!impl_->light_started) { impl_->light_started = true; impl_->start_light(); }
    vos_log("AR_ARKIT world_tracking=%d plane_detection=%d", (int)ar_world_tracking_provider_is_supported(),
            (int)ar_plane_detection_provider_is_supported());
}
void VosPlatform::Impl::start_planes() {
    plane_session = ar_session_create();
    ar_session_t ps = plane_session;
    VosPlatform::Impl *im = this;                        // the platform lives for the whole process
    ar_session_set_data_provider_state_change_handler(ps, nil, ^(ar_data_providers_t, ar_data_provider_state_t st, ar_error_t e, ar_data_provider_t) {
        vos_log("AR_PLANES state=%ld error=%ld", (long)st, e ? (long)ar_error_get_error_code(e) : 0L);
        if (st == ar_data_provider_state_stopped && e) im->placer.planes_unavailable();
    });
    ar_session_request_authorization(ps, ar_authorization_type_world_sensing, ^(ar_authorization_results_t results, ar_error_t error) {
        __block bool allowed = false;
        if (results) {
            ar_authorization_results_enumerate_results(results, ^bool(ar_authorization_result_t r) {
                if (ar_authorization_result_get_authorization_type(r) == ar_authorization_type_world_sensing)
                    allowed = ar_authorization_result_get_status(r) == ar_authorization_status_allowed;
                return true;
            });
        }
        vos_log("AR_PLANES world_sensing=%s error=%ld", allowed ? "allowed" : "denied", error ? (long)ar_error_get_error_code(error) : 0L);
        if (!allowed) { im->placer.planes_unavailable(); return; }
        im->placer.planes_authorized();
        ar_plane_detection_configuration_t pc = ar_plane_detection_configuration_create();
        ar_plane_detection_configuration_set_alignment(pc, ar_plane_alignment_horizontal);
        im->planes = ar_plane_detection_provider_create(pc);
        dispatch_queue_t pq = dispatch_queue_create("sm64ar.planes", DISPATCH_QUEUE_SERIAL);
        ar_plane_detection_provider_set_update_handler(im->planes, pq, ^(ar_plane_anchors_t added, ar_plane_anchors_t updated, ar_plane_anchors_t removed) {
            ar_plane_anchors_enumerator_t put = ^bool(ar_plane_anchor_t a) { im->plane_map[plane_uuid(a)] = plane_info(a); return true; };
            ar_plane_anchors_enumerate_anchors(added, put);
            ar_plane_anchors_enumerate_anchors(updated, put);
            ar_plane_anchors_enumerate_anchors(removed, ^bool(ar_plane_anchor_t a) { im->plane_map.erase(plane_uuid(a)); return true; });
            std::vector<PlaneInfo> v;
            for (const auto &kv : im->plane_map) v.push_back(kv.second);
            im->placer.set_planes(std::move(v));
        });
        ar_session_run(ps, ar_data_providers_create_with_data_providers(im->planes, nil));
    });
}
void VosPlatform::Impl::start_light() {
    const bool supported = ar_environment_light_estimation_provider_is_supported();
    const ar_authorization_type_t need = ar_environment_light_estimation_provider_get_required_authorization_type();
    vos_log("AR_LIGHT supported=%d auth_type=%lu", supported ? 1 : 0, (unsigned long)need);
    if (!supported) return;
    light_session = ar_session_create();
    VosPlatform::Impl *im = this;                         // the platform lives for the whole process
    ar_session_set_data_provider_state_change_handler(light_session, nil, ^(ar_data_providers_t, ar_data_provider_state_t st, ar_error_t e, ar_data_provider_t) {
        vos_log("AR_LIGHT state=%ld error=%ld", (long)st, e ? (long)ar_error_get_error_code(e) : 0L);
    });
    void (^run)(void) = ^{
        im->light = ar_environment_light_estimation_provider_create(ar_environment_light_estimation_configuration_create());
        dispatch_queue_t q = dispatch_queue_create("sm64ar.light", DISPATCH_QUEUE_SERIAL);
        ar_environment_light_estimation_provider_set_update_handler(im->light, q,
            ^(ar_environment_probe_anchors_t added, ar_environment_probe_anchors_t updated, ar_environment_probe_anchors_t removed) {
                const size_t na = ar_environment_probe_anchors_get_count(added), nu = ar_environment_probe_anchors_get_count(updated);
                ar_environment_probe_anchors_enumerator_t take = ^bool(ar_environment_probe_anchor_t a) { im->take_probe(a, false, na, nu); return true; };
                ar_environment_probe_anchors_enumerator_t drop = ^bool(ar_environment_probe_anchor_t a) { im->take_probe(a, true, na, nu); return true; };
                ar_environment_probe_anchors_enumerate_anchors(added, take);
                ar_environment_probe_anchors_enumerate_anchors(updated, take);
                ar_environment_probe_anchors_enumerate_anchors(removed, drop);
            });
        ar_session_run(im->light_session, ar_data_providers_create_with_data_providers(im->light, nil));
    };
    if (need == ar_authorization_type_none) { run(); return; }
    ar_session_request_authorization(light_session, need, ^(ar_authorization_results_t results, ar_error_t error) {
        __block bool allowed = false;
        if (results) {
            ar_authorization_results_enumerate_results(results, ^bool(ar_authorization_result_t r) {
                if (ar_authorization_result_get_authorization_type(r) == need)
                    allowed = ar_authorization_result_get_status(r) == ar_authorization_status_allowed;
                return true;
            });
        }
        vos_log("AR_LIGHT auth=%s error=%ld", allowed ? "allowed" : "denied", error ? (long)ar_error_get_error_code(error) : 0L);
        if (allowed) run();
    });
}
void VosPlatform::Impl::start_hands() {
    if (hands_started.exchange(true)) return;
    const bool supported = ar_hand_tracking_provider_is_supported();
    vos_log("AR_ARKIT hand_tracking=%s", supported ? "supported" : "unsupported");
    if (!supported) return;
    hand_session = ar_session_create();
    VosPlatform::Impl *im = this;                         // the platform lives for the whole process
    ar_session_set_data_provider_state_change_handler(hand_session, nil, ^(ar_data_providers_t, ar_data_provider_state_t st, ar_error_t e, ar_data_provider_t) {
        vos_log("AR_ARKIT hand_tracking state=%ld error=%ld", (long)st, e ? (long)ar_error_get_error_code(e) : 0L);
        if (st == ar_data_provider_state_stopped) im->hands_running = false;
    });
    ar_session_request_authorization(hand_session, ar_authorization_type_hand_tracking, ^(ar_authorization_results_t results, ar_error_t error) {
        __block bool allowed = false;
        if (results) {
            ar_authorization_results_enumerate_results(results, ^bool(ar_authorization_result_t r) {
                if (ar_authorization_result_get_authorization_type(r) == ar_authorization_type_hand_tracking)
                    allowed = ar_authorization_result_get_status(r) == ar_authorization_status_allowed;
                return true;
            });
        }
        vos_log("AR_ARKIT hand_tracking=%s error=%ld", allowed ? "authorized" : "denied", error ? (long)ar_error_get_error_code(error) : 0L);
        if (!allowed) return;
        im->hands = ar_hand_tracking_provider_create(ar_hand_tracking_configuration_create());
        ar_session_run(im->hand_session, ar_data_providers_create_with_data_providers(im->hands, nil));
        im->hands_running.store(true, std::memory_order_release);
    });
}
void VosPlatform::Impl::take_hands(double t) {
    HandSink sink;
    { std::lock_guard<std::mutex> g(claim_m); sink = hand_sink; }
    if (!sink) return;
    if (!hands_running.load(std::memory_order_acquire)) {
        if (hands_fed) {
            const HandTips none[2];
            sink(none);
            hands_fed = false;
            vos_log("AR_ARKIT hand_tracking stopped: hands cleared (relaunch to track them again)");
        }
        return;
    }
    if (!hand_left) { hand_left = ar_hand_anchor_create(); hand_right = ar_hand_anchor_create(); }
    HandTips out[2];
    const bool ok = ar_hand_tracking_provider_query_anchors_at_timestamp(hands, t, hand_left, hand_right) == ar_hand_anchor_query_status_success;
    if (ok && placer.valid()) {
        const ar_pose tft = placer.tracking_from_table();
        ar_hand_anchor_t a[2] = {hand_left, hand_right};
        for (int i = 0; i < 2; i++) {
            if (!ar_hand_anchor_is_tracked(a[i])) continue;
            ar_hand_skeleton_t sk = ar_hand_anchor_get_hand_skeleton(a[i]);
            if (!sk) continue;
            ar_skeleton_joint_t ji = ar_hand_skeleton_get_joint_named(sk, ar_hand_skeleton_joint_name_index_finger_tip);
            if (!ji || !ar_skeleton_joint_is_tracked(ji)) continue;
            const simd_float4x4 oa = ar_hand_anchor_get_origin_from_anchor_transform(a[i]);
            const simd_float4x4 ai = ar_skeleton_joint_get_anchor_from_joint_transform(ji);
            float m0[16], m1[16];
            std::memcpy(m0, &oa, sizeof m0); std::memcpy(m1, &ai, sizeof m1);
            out[i] = hand_tips_in_table(tft, m0, m1);
        }
        if ((out[0].tracked || out[1].tracked) && (hand_frames++ == 0))
            vos_log("AR_ARKIT hand_tracking first tracked hand: left=%d right=%d", out[0].tracked ? 1 : 0, out[1].tracked ? 1 : 0);
    }
    sink(out);
    hands_fed = true;
}
void VosPlatform::Impl::take_probe(ar_environment_probe_anchor_t a, bool removed, size_t added, size_t updated) {
    id<MTLTexture> t = removed ? nil : ar_environment_probe_anchor_get_environment_texture(a);
    const double now = ar_environment_probe_anchor_get_timestamp(a);
    uuid_t id;
    ar_anchor_get_identifier((ar_anchor_t)a, id);
    char name[37];
    uuid_unparse_lower(id, name);
    uint64_t n;
    size_t probes;
    {
        std::lock_guard<std::mutex> g(light_m);
        n = ++light_updates;
        // Per anchor: a nil texture (a dim room) or a removal drops only that anchor's probe, never another's.
        if (t) light_probes[name] = Probe{a, n}; else light_probes.erase(name);
        const Probe *best = nullptr;
        for (const auto &kv : light_probes) if (!best || kv.second.seq > best->seq) best = &kv.second;
        ar_environment_probe_anchor_t want = best ? best->anchor : nil;
        const bool turned = want != light_latest;
        light_latest = want;
        if (want && (t || turned)) light_generation++;
        probes = light_probes.size();
    }
    if (n <= 5 || n % 100 == 0)
        vos_log("AR_LIGHT update n=%llu anchor=%.8s added=%zu updated=%zu probes=%zu removed=%d dt=%.3f tex=%d fmt=%lu size=%lu mips=%lu usage=%lu storage=%lu scale=%.5g",
                (unsigned long long)n, name, added, updated, probes, removed ? 1 : 0, now - light_last_s, t ? 1 : 0,
                t ? (unsigned long)t.pixelFormat : 0UL, t ? (unsigned long)t.width : 0UL, t ? (unsigned long)t.mipmapLevelCount : 0UL,
                t ? (unsigned long)t.usage : 0UL, t ? (unsigned long)t.storageMode : 0UL,
                (double)ar_environment_probe_anchor_get_camera_scale_reference(a));
    light_last_s = now;
}
void VosPlatform::detach() {
    impl_->layer = nil;
    impl_->frame = nullptr;
    impl_->drawable = nullptr;
    impl_->running = false;
}
void *VosPlatform::metal_device() const { return impl_->layer ? (__bridge void *)cp_layer_renderer_get_device(impl_->layer) : nullptr; }
bool VosPlatform::external_view(uint32_t i, ExternalView *ev) const {
    cp_drawable_t d = impl_->drawable;
    if (!d || i >= cp_drawable_get_view_count(d)) return false;
    cp_view_texture_map_t tm = cp_view_get_view_texture_map(cp_drawable_get_view(d, i));
    const size_t ti = cp_view_texture_map_get_texture_index(tm);
    const MTLViewport vp = cp_view_texture_map_get_viewport(tm);
    ev->color = (__bridge void *)cp_drawable_get_color_texture(d, ti);
    ev->depth = (__bridge void *)cp_drawable_get_depth_texture(d, ti);
    ev->slice = (uint32_t)cp_view_texture_map_get_slice_index(tm);
    ev->viewport = Viewport{(float)vp.originX, (float)vp.originY, (float)vp.width, (float)vp.height};
    return true;
}
bool VosPlatform::rate_map(RateMap *out) const {
    *out = RateMap{};
    cp_drawable_t d = impl_->drawable;
    if (!d) return false;
    out->maps = (uint32_t)cp_drawable_get_rasterization_rate_map_count(d);
    if (out->maps == 0) return false;
    id<MTLRasterizationRateMap> m = cp_drawable_get_rasterization_rate_map(d, 0);
    if (!m) return false;
    out->map = (__bridge void *)m;
    out->layers = (uint32_t)m.layerCount;
    out->screen[0] = (uint32_t)m.screenSize.width;
    out->screen[1] = (uint32_t)m.screenSize.height;
    for (uint32_t i = 0; i < out->layers && i < AR_MAX_VIEWS; i++) {
        const MTLSize p = [m physicalSizeForLayer:i];
        out->physical[i][0] = (uint32_t)p.width;
        out->physical[i][1] = (uint32_t)p.height;
    }
    id<MTLTexture> c = cp_drawable_get_color_texture(d, 0);
    out->texture[0] = c ? (uint32_t)c.width : 0;
    out->texture[1] = c ? (uint32_t)c.height : 0;
    return true;
}
void VosPlatform::set_render_quality(float q) {
    if (!impl_->layer || !(q > 0.f)) return;
    cp_layer_renderer_t lr = impl_->layer;
    const float max_q = cp_layer_renderer_configuration_get_max_render_quality(cp_layer_renderer_get_configuration(lr));
    if (!(max_q > 0.f)) {
        vos_log("AR_QUALITY requested=%g ignored: no max render quality configured", (double)q);
        return;
    }
    const float before = cp_layer_renderer_get_render_quality(lr);
    cp_layer_renderer_set_render_quality(lr, std::min(q, max_q));
    vos_log("AR_QUALITY requested=%g max=%g before=%g now=%g", (double)q, (double)max_q, (double)before,
            (double)cp_layer_renderer_get_render_quality(lr));
}
bool VosPlatform::layered() const {
    return impl_->layer &&
           cp_layer_renderer_configuration_get_layout(cp_layer_renderer_get_configuration(impl_->layer)) == cp_layer_renderer_layout_layered;
}
bool VosPlatform::color_wants_linear() const {
    return impl_->drawable && vos_format_wants_linear((uint32_t)cp_drawable_get_color_texture(impl_->drawable, 0).pixelFormat);
}
uint32_t VosPlatform::color_format() const { return impl_->drawable ? (uint32_t)cp_drawable_get_color_texture(impl_->drawable, 0).pixelFormat : 0; }
void VosPlatform::encode_present(void *cb) { if (impl_->drawable) cp_drawable_encode_present(impl_->drawable, (__bridge id<MTLCommandBuffer>)cb); }
void VosPlatform::end_submission() {
    if (impl_->frame) cp_frame_end_submission(impl_->frame);
    impl_->frame = nullptr;
    impl_->drawable = nullptr;
}
void VosPlatform::set_pads(VosPads *p) { impl_->pads = p; }
void VosPlatform::set_holding(bool on) { impl_->holding = on; }
void VosPlatform::set_background(bool on) { impl_->background = on; }
void VosPlatform::space_closed() { impl_->closed = true; }
bool VosPlatform::placed() const { return impl_->placer.valid(); }
void VosPlatform::replace() { impl_->placer.request_replace(); }
bool VosPlatform::pad_lost() const { return impl_->pad_lost; }
void VosPlatform::set_replaying(bool on) { impl_->replaying = on; }
std::string VosPlatform::placement_hint() const { return impl_->placer.hint(); }
void VosPlatform::push_spatial_event(const SpatialEvent &e) { impl_->input.push(e); }
void VosPlatform::set_pointer_claim(PointerClaim c, bool pose_only) {
    std::lock_guard<std::mutex> g(impl_->claim_m);
    impl_->claim = std::move(c);
    impl_->claim_pose_only = pose_only;
}
void VosPlatform::start_hand_tracking() { impl_->start_hands(); }
void VosPlatform::set_hand_sink(HandSink sink) { std::lock_guard<std::mutex> g(impl_->claim_m); impl_->hand_sink = std::move(sink); }
void VosPlatform::set_table_placed_sink(std::function<void()> sink) {
    std::lock_guard<std::mutex> g(impl_->claim_m); impl_->table_placed_sink = std::move(sink);
}
}
