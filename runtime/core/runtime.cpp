#include "ar_runtime.h"
#include <condition_variable>
#include <mutex>
#include <thread>

struct ar_runtime {
    const ar_adapter *adapter = nullptr;
    // interpolation (render thread only): the snapshot seq last seen and the display time it was first shown at.
    uint64_t interp_seq = 0;
    int64_t interp_arrival = 0;
    bool interp_seen = false;
    const ar_platform *platform = nullptr;
    ar_texture_upload_fn sink = nullptr;
    void *sink_user = nullptr;
    std::mutex m;
    std::condition_variable cv;
    ar_pose head_tracking = ar_pose_identity();
    ar_pose tracking_from_table = ar_pose_identity();
    float table_half[2] = {0.f, 0.f};
    bool table_seen = false;
    ar_level_info level{8000.f, 0.f, {0, 0, 0}, {0, 0, 0}};
    uint32_t due = 0;
    uint64_t requested = 0, done = 0;
    bool have_t0 = false;
    int64_t t0 = 0;
    bool stop = false;
    std::thread game;
    const sarec *replay = nullptr;
    sarec *recorder = nullptr;
    ar_replay_status rstat{};
    uint32_t audio_mode = AR_AUDIO_MODE_STEREO;
    bool game_started = false;
};

extern "C" {
ar_runtime *ar_runtime_create(const ar_adapter *a, const ar_platform *p) {
    if (!a || !p || a->abi_version != AR_ADAPTER_ABI_VERSION || p->abi_version != AR_PLATFORM_ABI_VERSION) return nullptr;
    if (!p->wait_frame || !p->get_table || !p->get_pad || !p->session_state) return nullptr;
    // audio_pull is optional; every other adapter entry is called unconditionally.
    if (!a->init || !a->last_error || !a->tick || !a->acquire_snapshot || !a->release_snapshot ||
        !a->drain_texture_uploads || !a->level_info || !a->state_hash) return nullptr;
    auto *rt = new ar_runtime();
    rt->adapter = a;
    rt->platform = p;
    return rt;
}
void ar_runtime_destroy(ar_runtime *rt) {
    if (!rt) return;
    ar_runtime_stop_game_thread(rt);
    delete rt;
}
int ar_runtime_init_game(ar_runtime *rt, const char *rom, const char *save) {
    rt->game_started = true;
    if (!rt->adapter->init(rom, save)) return 0;
    ar_level_info lv = rt->adapter->level_info();
    std::lock_guard<std::mutex> g(rt->m);
    rt->level = lv;
    return 1;
}
void ar_runtime_set_texture_sink(ar_runtime *rt, ar_texture_upload_fn fn, void *user) { rt->sink = fn; rt->sink_user = user; }

void ar_runtime_step_game(ar_runtime *rt) {
    ar_pose head, table;
    const sarec *rep;
    sarec *rec;
    uint64_t idx;
    {
        std::lock_guard<std::mutex> g(rt->m);
        head = rt->head_tracking;
        table = rt->tracking_from_table;
        if (rt->due) rt->due--;
        rep = rt->replay; rec = rt->recorder; idx = rt->done;
    }
    ar_player_input in{};
    in.present = 1;
    in.head_in_table = ar_pose_mul(ar_pose_inverse(table), head);
    in.input_frame_in_table = in.head_in_table;
    const sarec_tick *want = nullptr;
    if (rep) {
        in.pad_connected = 1;
        const bool pin = (rep->flags & SAREC_FLAG_INPUT_FRAME) != 0;
        if (idx < rep->count) {
            want = &rep->ticks[idx];
            in.pad = want->pad;
            if (pin) in.input_frame_in_table = want->input_frame;
        } else if (pin && rep->count) {
            in.input_frame_in_table = rep->ticks[rep->count - 1].input_frame;
        }
    } else {
        in.pad_connected = rt->platform->get_pad(rt->platform->user, 0, &in.pad) ? 1 : 0;
        if (!in.pad_connected) in.pad = ar_pad{};
    }
    rt->adapter->tick(&in, 1);
    const bool check = want && (rep->flags & SAREC_FLAG_HASHES);
    const uint64_t hash = (check || rec) ? rt->adapter->state_hash() : 0;
    if (rec) {
        sarec_tick t{};
        t.tick = (uint32_t)idx; t.pad = in.pad; t.head = in.head_in_table; t.input_frame = in.input_frame_in_table;
        t.table = table; t.state_hash = hash;
        sarec_push(rec, t);
        rec->flags |= SAREC_FLAG_HASHES | SAREC_FLAG_INPUT_FRAME;
    }
    ar_level_info lv = rt->adapter->level_info();
    std::lock_guard<std::mutex> g(rt->m);
    rt->level = lv;
    rt->done++;
    if (rep) {
        ar_replay_status &s = rt->rstat;
        if (want) s.played = idx + 1;
        if (check) {
            s.checked++;
            if (!s.diverged && hash != want->state_hash) {
                s.diverged = 1; s.first_bad_tick = idx; s.expected = want->state_hash; s.got = hash;
            }
        }
        s.done = idx + 1 >= rep->count ? 1 : 0;
    }
}
void ar_runtime_set_replay(ar_runtime *rt, const sarec *rec) {
    std::lock_guard<std::mutex> g(rt->m);
    rt->replay = rec;
    rt->rstat = ar_replay_status{};
}
ar_replay_status ar_runtime_replay_status(ar_runtime *rt) { std::lock_guard<std::mutex> g(rt->m); return rt->rstat; }
void ar_runtime_set_recorder(ar_runtime *rt, sarec *out) { std::lock_guard<std::mutex> g(rt->m); rt->recorder = out; }
uint32_t ar_runtime_ticks_due(ar_runtime *rt) { std::lock_guard<std::mutex> g(rt->m); return rt->due; }
uint64_t ar_runtime_ticks_done(ar_runtime *rt) { std::lock_guard<std::mutex> g(rt->m); return rt->done; }

int ar_runtime_start_game_thread(ar_runtime *rt) {
    if (rt->game.joinable()) return 0;
    rt->stop = false;
    rt->game = std::thread([rt] {
        for (;;) {
            {
                std::unique_lock<std::mutex> l(rt->m);
                rt->cv.wait(l, [rt] { return rt->stop || rt->due > 0; });
                if (rt->stop) return;
            }
            ar_runtime_step_game(rt);
        }
    });
    return 1;
}
void ar_runtime_stop_game_thread(ar_runtime *rt) {
    { std::lock_guard<std::mutex> g(rt->m); rt->stop = true; }
    rt->cv.notify_all();
    if (rt->game.joinable()) rt->game.join();
}

static const uint64_t AR_MAX_CATCHUP_TICKS = 4;

float ar_interp_alpha(int64_t display_ns, int64_t arrival_ns) {
    const double a = (double)(display_ns - arrival_ns) * 30.0 / 1e9;
    return a <= 0.0 ? 0.f : a >= 1.0 ? 1.f : (float)a;
}

int ar_runtime_begin_frame(ar_runtime *rt, ar_render_frame *out) {
    const ar_platform *p = rt->platform;
    ar_frame_info fi{};
    *out = ar_render_frame{};   // a failed begin leaves no snapshot, so a following end_frame is a no-op
    if (!p->wait_frame(p->user, &fi)) return 0;
    if (p->begin_frame) p->begin_frame(p->user);
    ar_table tb{};
    tb.tracking_from_table = ar_pose_identity();
    const int have_table = p->get_table(p->user, &tb);
    const bool state_focused = p->session_state(p->user) == AR_SESSION_FOCUSED;
    ar_level_info lv;
    ar_pose table;
    bool table_seen;
    float half[2];
    {
        std::lock_guard<std::mutex> g(rt->m);
        if (have_table) {
            rt->tracking_from_table = tb.tracking_from_table;
            rt->table_half[0] = tb.half_extent[0]; rt->table_half[1] = tb.half_extent[1];
            rt->table_seen = true;
        }
        table = rt->tracking_from_table;
        table_seen = rt->table_seen;
        half[0] = rt->table_half[0]; half[1] = rt->table_half[1];
        const bool focused = state_focused && table_seen;
        rt->head_tracking = fi.tracking_from_head;
        if (!rt->have_t0) { rt->have_t0 = true; rt->t0 = fi.display_time_ns; }
        if (focused) {
            int64_t dt = fi.display_time_ns - rt->t0;
            uint64_t total = (uint64_t)(dt < 0 ? 0 : dt * 30 / 1000000000LL) + 1;
            if (total > rt->requested + AR_MAX_CATCHUP_TICKS) {   // long stall: drop the excess, don't burst
                uint64_t excess = total - rt->requested - AR_MAX_CATCHUP_TICKS;
                rt->t0 += (int64_t)excess * 1000000000LL / 30;
                total -= excess;
            }
            if (total > rt->requested) { rt->due += (uint32_t)(total - rt->requested); rt->requested = total; }
        } else {
            rt->due = 0;
            // paused: slide the clock so the next focused frame continues from the last requested tick
            rt->t0 = fi.display_time_ns - (int64_t)(rt->requested ? rt->requested - 1 : 0) * 1000000000LL / 30;
        }
        lv = rt->level;
    }
    rt->cv.notify_all();

    out->snapshot = rt->adapter->acquire_snapshot();
    const fc_frame *snap = out->snapshot;
    const bool v5 = snap && snap->abi_version >= 5u;
    if (v5 && snap->has_level) lv = snap->level;            // the frame's own level (no mixed-tick frames)
    out->interp_alpha = 1.f;
    if (v5 && snap->prev_pos) {
        if (!rt->interp_seen || snap->seq != rt->interp_seq) {
            rt->interp_seen = true; rt->interp_seq = snap->seq; rt->interp_arrival = fi.display_time_ns;
        }
        out->interp_alpha = ar_interp_alpha(fi.display_time_ns, rt->interp_arrival);
    }

    out->display_time_ns = fi.display_time_ns;
    out->tracking_from_table = table;
    out->level = lv;
    out->table_valid = table_seen ? 1 : 0;
    out->head_tracked = fi.head_tracked;
    out->display_hz = fi.display_hz;
    out->has_environment = 0;
    if (p->get_environment && p->get_environment(p->user, &out->environment)) out->has_environment = 1;
    out->table_half_extent[0] = half[0]; out->table_half_extent[1] = half[1];
    out->view_count = fi.should_render ? (fi.view_count > AR_MAX_VIEWS ? AR_MAX_VIEWS : fi.view_count) : 0;
    ar_mat4 tracking_from_table = ar_mat4_from_pose(table);
    ar_mat4 table_from_level = ar_table_from_level(lv.units_per_meter, lv.ground_y);
    ar_pose table_from_tracking = ar_pose_inverse(table);
    uint32_t x = 0;
    for (uint32_t i = 0; i < out->view_count; i++) {
        const ar_view &v = fi.views[i];
        ar_mat4 eye_from_tracking = ar_mat4_inverse_rigid(ar_mat4_from_pose(v.tracking_from_eye));
        const float near_m = v.near_m > 0.f ? v.near_m : AR_NEAR_M;
        ar_mat4 clip_from_tracking = ar_mat4_mul(ar_proj_reverse_z(v.fov, near_m), eye_from_tracking);
        out->views[i].tracking_from_eye = v.tracking_from_eye;
        out->views[i].fov = v.fov;
        out->views[i].near_m = near_m;
        out->views[i].clip_from_tracking = clip_from_tracking;
        out->views[i].clip_from_table = ar_mat4_mul(clip_from_tracking, tracking_from_table);
        out->views[i].clip_from_level = ar_mat4_mul(out->views[i].clip_from_table, table_from_level);
        ar_pose eye_table = ar_pose_mul(table_from_tracking, v.tracking_from_eye);
        ar_level_from_table_point(lv.units_per_meter, lv.ground_y, eye_table.pos, out->views[i].eye_level);
        out->views[i].x = x; out->views[i].width = v.width; out->views[i].height = v.height;
        x += v.width;
    }
    out->preview = ar_preview{};
    if (p->get_preview && !p->get_preview(p->user, &out->preview)) out->preview = ar_preview{};
    if (out->preview.outline_count > AR_PREVIEW_MAX_OUTLINES) out->preview.outline_count = AR_PREVIEW_MAX_OUTLINES;
    ar_pose head_table = ar_pose_mul(table_from_tracking, fi.tracking_from_head);
    out->head_table = head_table;
    ar_level_from_table_point(lv.units_per_meter, lv.ground_y, head_table.pos, out->head_level);
    if (out->snapshot && rt->sink) rt->adapter->drain_texture_uploads(out->snapshot->seq, rt->sink, rt->sink_user);
    return 1;
}
void ar_runtime_end_frame(ar_runtime *rt, const ar_render_frame *f) {
    if (f && f->snapshot) rt->adapter->release_snapshot(f->snapshot);
}
void ar_runtime_submit_frame(ar_runtime *rt, const ar_render_frame *f) {
    const ar_platform *p = rt->platform;
    if (!f || !p->end_frame) return;
    ar_submit_info s{};
    s.display_time_ns = f->display_time_ns;
    s.view_count = f->view_count;
    for (uint32_t i = 0; i < f->view_count; i++) {
        s.views[i].tracking_from_eye = f->views[i].tracking_from_eye;
        s.views[i].fov = f->views[i].fov;
        s.views[i].width = f->views[i].width;
        s.views[i].height = f->views[i].height;
        s.views[i].near_m = f->views[i].near_m;
    }
    s.tracking_from_table = f->tracking_from_table;
    s.table_valid = f->table_valid;
    p->end_frame(p->user, &s);
}
int ar_runtime_set_audio_mode(ar_runtime *rt, uint32_t mode) {
    if (!rt || rt->game_started) return 0;
    if (mode != AR_AUDIO_MODE_STEREO && mode != AR_AUDIO_MODE_SOURCES) return 0;
    if (!rt->adapter->audio_set_mode) { if (mode != AR_AUDIO_MODE_STEREO) return 0; rt->audio_mode = mode; return 1; }
    rt->adapter->audio_set_mode(mode);
    rt->audio_mode = mode;
    return 1;
}
uint32_t ar_runtime_audio_mode(ar_runtime *rt) { return rt ? rt->audio_mode : AR_AUDIO_MODE_STEREO; }
uint32_t ar_runtime_pull_audio_scene(ar_runtime *rt, ar_audio_pull *p) {
    p->meta_count = 0;
    return rt->adapter->audio_pull ? rt->adapter->audio_pull(p) : 0;
}
uint32_t ar_runtime_pull_audio(ar_runtime *rt, int16_t *stereo, uint32_t frames) {
    ar_audio_pull p{};
    p.bed = stereo;
    p.frames = frames;
    return ar_runtime_pull_audio_scene(rt, &p);
}
}
