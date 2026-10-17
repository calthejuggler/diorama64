#import <AVFAudio/AVFAudio.h>
#import <PHASE/PHASE.h>
#include "vos_phase.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <TargetConditionals.h>
#include "vos_log.h"
#include "vos_scene_audio.h"
#include "vos_scene_pos.h"
extern "C" {
#include "ar_audio_distance.h"
#include "ar_math.h"
}
namespace ar {
namespace {
simd_float4x4 to_simd(const ar_pose &p) {
    const ar_mat4 m = ar_mat4_from_pose(p);
    simd_float4x4 r;
    for (int c = 0; c < 4; c++) r.columns[c] = simd_make_float4(m.m[c * 4], m.m[c * 4 + 1], m.m[c * 4 + 2], m.m[c * 4 + 3]);
    return r;
}
simd_float4x4 translation(const float p[3]) {
    simd_float4x4 r = matrix_identity_float4x4;
    r.columns[3] = simd_make_float4(p[0], p[1], p[2], 1.f);
    return r;
}
uint32_t pull_scene(void *u, ar_audio_pull *p) { return ar_runtime_pull_audio_scene(static_cast<ar_runtime *>(u), p); }
PHASESpatializationMode phase_mode(AudioSpatializer m) {
    switch (m) {
    case AudioSpatializer::automatic: return PHASESpatializationModeAutomatic;
    case AudioSpatializer::channel: return PHASESpatializationModeAlwaysUseChannelBased;
    case AudioSpatializer::binaural: break;
    }
    return PHASESpatializationModeAlwaysUseBinaural;
}
const char *mode_name(PHASESpatializationMode m) {
    return m == PHASESpatializationModeAlwaysUseBinaural ? "binaural" : m == PHASESpatializationModeAlwaysUseChannelBased ? "channel" : "automatic";
}
std::string why(NSError *e) { return e ? std::string(e.localizedDescription.UTF8String) : std::string("unknown error"); }
SceneFeed::Config feed_config(float tap_s, bool poly) { SceneFeed::Config c; c.tap_seconds = tap_s; c.polyphase = poly; return c; }
}
struct VosPhase::Impl {
    Impl(ar_runtime *r, float tap_s, bool poly) : rt(r), feed(feed_config(tap_s, poly), pull_scene, r) {}
    ar_runtime *rt;
    SceneFeed feed;
    ScenePositions positions;                      // layer thread only
    ar_audio_tuning tuning{1.f};
    uint32_t gain_id[AR_AUDIO_MAX_SOURCES] = {};
    AudioSpatializer spatializer = AudioSpatializer::binaural;
    ar_pose dbg_table{}, dbg_listener{};
    float dbg_pos[AR_AUDIO_MAX_SOURCES][3] = {};
    bool dbg_active[AR_AUDIO_MAX_SOURCES] = {};
    uint32_t dbg_flags[AR_AUDIO_MAX_SOURCES] = {};
    float dbg_level[AR_AUDIO_MAX_SOURCES] = {};
    PHASEEngine *engine = nil;
    PHASEListener *listener = nil;
    PHASEObject *table = nil;
    NSMutableArray<PHASESource *> *sources = nil;
    NSMutableArray<PHASESoundEvent *> *events = nil;
    bool client = false, automatic = false;
    PhaseRung rung = PhaseRung::local;
    std::atomic<uint64_t> bed_calls{0}, out_frames{0};
    std::atomic<uint32_t> cadence_n[2] = {{0}, {0}};
    double cadence_t[2][8] = {};
    uint32_t cadence_f[2][8] = {};
    bool cadence_logged = false;
    uint32_t lanes_active = 0;
    uint64_t dropped_sources = 0;
    std::atomic<uint32_t> voices_failed{0};
    std::mutex ctl;                                 // control state (layer thread, session notifications); never in a render block
    bool running = false, attached = true, interrupted = false;
    id interruption = nil, route_change = nil;
    bool try_rung(const PhaseRungPlan &p, AudioSpatializer spatializer, std::string *why_failed);
    void teardown();
    void note_cadence(int which, const AudioTimeStamp *ts, AVAudioFrameCount n) {
        const uint32_t i = cadence_n[which].load(std::memory_order_relaxed);
        if (i >= 8) return;
        cadence_t[which][i] = ts ? ts->mSampleTime : -1.0;
        cadence_f[which][i] = n;
        cadence_n[which].store(i + 1, std::memory_order_release);
    }
};
VosPhase::VosPhase(ar_runtime *rt, float tap_s, bool poly) : impl_(new Impl(rt, tap_s, poly)) {}
VosPhase::~VosPhase() { stop(); delete impl_; }
// One engine-bound object graph per rung; a failed rung tears all of it down, so the next rung starts from nothing.
// Engine-independent state (the feed, positions, tuning, gain ids, the spatializer) is kept.
void VosPhase::Impl::teardown() {
    // The observers are read and removed before ctl is taken (their handlers take ctl): teardown runs on the setup thread
    // before any observer exists (a failed rung), or on the layer thread once nothing else calls this path (stop, the
    // destructor, a voice-failure fallback). running is cleared below; attached and interrupted need no reset (no engine).
    if (interruption) { [[NSNotificationCenter defaultCenter] removeObserver:interruption]; interruption = nil; }
    if (route_change) { [[NSNotificationCenter defaultCenter] removeObserver:route_change]; route_change = nil; }
    std::lock_guard<std::mutex> g(ctl);
    for (PHASESoundEvent *ev in events) [ev stopAndInvalidate];
    if (engine) [engine stop];
    events = nil;
    sources = nil;
    table = nil;
    listener = nil;
    engine = nil;
    running = false;
}
bool VosPhase::Impl::try_rung(const PhaseRungPlan &p, AudioSpatializer spatializer, std::string *why_failed) {
    NSError *e = nil;
    auto fail = [&](const char *what, NSError *err) {
        *why_failed = std::string(what) + ": " + why(err);
        teardown();
        return false;
    };
    if (p.rung == PhaseRung::client) {
        if (@available(visionOS 26.0, *)) {
            engine = [[PHASEEngine alloc] initWithUpdateMode:PHASEUpdateModeManual renderingMode:PHASERenderingModeClient];
        } else {
            *why_failed = "client rendering needs visionOS 26";
            return false;
        }
    } else {
        engine = [[PHASEEngine alloc] initWithUpdateMode:PHASEUpdateModeManual];
    }
    client = p.rung == PhaseRung::client;
    // Before start: the header gives no rule for when it may change (Automatic may pick channel panning on the
    // headset's speakers).
    engine.outputSpatializationMode = phase_mode(spatializer);
    if (![engine startAndReturnError:&e]) return fail("engine", e);
    engine.unitsPerMeter = 1.0;
    listener = [[PHASEListener alloc] initWithEngine:engine];
    automatic = false;
    if (p.automatic_head) {   // set before any addChild (the listener's tracking mode, then its place in the scene)
        if (@available(visionOS 26.0, *)) {
            listener.automaticHeadTrackingFlags = PHASEAutomaticHeadTrackingFlagOrientation | PHASEAutomaticHeadTrackingFlagPosition;
            automatic = true;
        }
    }
    // A device's client engine refused our listener as a child ("Cannot add PHASESharedListener as child"):
// that rung leaves it unparented and only the mixer parameters reference it.
    e = nil;
    if (p.listener_child && ![engine.rootObject addChild:listener error:&e]) return fail("listener", e);
    table = [[PHASEObject alloc] initWithEngine:engine];
    e = nil;
    if (![engine.rootObject addChild:table error:&e]) return fail("table object", e);
    NSMutableArray<PHASEEnvelopeSegment *> *segs = [NSMutableArray array];
    for (int i = 1; i < AR_AUDIO_DISTANCE_POINTS; i++)
        [segs addObject:[[PHASEEnvelopeSegment alloc] initWithEndPoint:simd_make_double2(ar_audio_distance_curve[i][0], ar_audio_distance_curve[i][1])
                                                             curveType:PHASECurveTypeLinear]];
    PHASEEnvelope *curve = [[PHASEEnvelope alloc] initWithStartPoint:simd_make_double2(ar_audio_distance_curve[0][0], ar_audio_distance_curve[0][1])
                                                            segments:segs];
    AVAudioFormat *mono = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32 sampleRate:48000 channels:1 interleaved:NO];
    AVAudioFormat *stereo = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32 sampleRate:48000 channels:2 interleaved:NO];
    sources = [NSMutableArray array];
    events = [NSMutableArray array];
    Impl *im = this;
    for (uint32_t l = 0; l < AR_AUDIO_MAX_SOURCES; l++) {
        NSString *name = [NSString stringWithFormat:@"lane%u", l];
        PHASESpatialPipeline *pipe = [[PHASESpatialPipeline alloc] initWithFlags:PHASESpatialPipelineFlagDirectPathTransmission];
        PHASESpatialMixerDefinition *mix = [[PHASESpatialMixerDefinition alloc] initWithSpatialPipeline:pipe identifier:name];
        mix.distanceModelParameters = [[PHASEEnvelopeDistanceModelParameters alloc] initWithEnvelope:curve];
        PHASEPullStreamNodeDefinition *node = [[PHASEPullStreamNodeDefinition alloc] initWithMixerDefinition:mix format:mono
                                                                                                   identifier:[name stringByAppendingString:@".pull"]];
        NSString *asset = [name stringByAppendingString:@".event"];
        e = nil;
        if (![engine.assetRegistry registerSoundEventAssetWithRootNode:node identifier:asset error:&e]) return fail("asset", e);
        PHASESource *src = [[PHASESource alloc] initWithEngine:engine];
        e = nil;
        if (![table addChild:src error:&e]) return fail("source", e);
        PHASEMixerParameters *mp = [[PHASEMixerParameters alloc] init];
        [mp addSpatialMixerParametersWithIdentifier:mix.identifier source:src listener:listener];
        e = nil;
        PHASESoundEvent *ev = [[PHASESoundEvent alloc] initWithEngine:engine assetIdentifier:asset mixerParameters:mp error:&e];
        if (!ev) return fail("sound event", e);
        const uint32_t ch = 2 + l;
        const int cadence = l == 0 ? 1 : -1;
        ev.pullStreamNodes[node.identifier].renderBlock =
            ^OSStatus(BOOL *isSilence, const AudioTimeStamp *ts, AVAudioFrameCount n, AudioBufferList *out) {
                if (cadence >= 0) im->note_cadence(cadence, ts, n);
                float *dst = static_cast<float *>(out->mBuffers[0].mData);
                const uint64_t k = im->bed_calls.load(std::memory_order_acquire);
                if (k == 0) {   // no lane opens a cycle: before the bed's first call there is nothing to copy
                    std::memset(dst, 0, sizeof(float) * n);
                    *isSilence = YES;
                    return noErr;
                }
                bool silent = true;   // lanes read the cycle the bed's last call opened
                im->feed.render(k, n, ch, dst, &silent);
                *isSilence = silent;
                return noErr;
            };
        [sources addObject:src];
        [events addObject:ev];
    }
    AVAudioChannelLayout *layout = [[AVAudioChannelLayout alloc] initWithLayoutTag:kAudioChannelLayoutTag_Stereo];
    PHASEChannelMixerDefinition *bedMix = [[PHASEChannelMixerDefinition alloc] initWithChannelLayout:layout identifier:@"bed"];
    PHASEPullStreamNodeDefinition *bedNode = [[PHASEPullStreamNodeDefinition alloc] initWithMixerDefinition:bedMix format:stereo identifier:@"bed.pull"];
    e = nil;
    if (![engine.assetRegistry registerSoundEventAssetWithRootNode:bedNode identifier:@"bed.event" error:&e]) return fail("bed asset", e);
    e = nil;
    PHASESoundEvent *bed = [[PHASESoundEvent alloc] initWithEngine:engine assetIdentifier:@"bed.event" mixerParameters:[[PHASEMixerParameters alloc] init] error:&e];
    if (!bed) return fail("bed event", e);
    bed.pullStreamNodes[bedNode.identifier].renderBlock =
        ^OSStatus(BOOL *isSilence, const AudioTimeStamp *ts, AVAudioFrameCount n, AudioBufferList *out) {
            im->note_cadence(0, ts, n);
            const uint64_t key = im->bed_calls.fetch_add(1, std::memory_order_acq_rel) + 1;   // the bed opens each cycle
            bool sl = true, sr = true;
            im->feed.render(key, n, 0, static_cast<float *>(out->mBuffers[0].mData), &sl);
            im->feed.render(key, n, 1, static_cast<float *>(out->mBuffers[1].mData), &sr);
            *isSilence = sl && sr;
            im->out_frames.fetch_add(n, std::memory_order_relaxed);
            return noErr;
        };
    [events addObject:bed];
    return true;
}
bool VosPhase::start_engine(const std::vector<PhaseRungPlan> &rungs, AudioSpatializer spatializer, std::string *err) {
    Impl *im = impl_;
    if (rungs.empty()) { *err = "no PHASE rung for this --phase-mode"; return false; }
    NSError *e = nil;
    AVAudioSession *as = [AVAudioSession sharedInstance];
    if (![as setCategory:AVAudioSessionCategoryAmbient error:&e] || ![as setActive:YES error:&e]) { *err = "AVAudioSession: " + why(e); return false; }
#if TARGET_OS_VISION
    {   // PHASE renders binaural itself: the system must not spatialize its output again
        NSError *se = nil;
        const BOOL ok = [as setIntendedSpatialExperience:AVAudioSessionSpatialExperienceBypassed options:@{} error:&se];
        vos_log("AR_AUDIO spatial=bypassed ok=%d%s%s", (int)ok, se ? " error=" : "", se ? why(se).c_str() : "");
    }
#endif
    std::string last;
    bool ok = false;
    for (const PhaseRungPlan &p : rungs) {
        std::string reason;
        ok = im->try_rung(p, spatializer, &reason);
        vos_log("AR_AUDIO phase rung=%s head=%s listener=%s result=%s%s%s", phase_rung_name(p.rung), p.automatic_head ? "automatic" : "manual",
                p.listener_child ? "child" : "shared", ok ? "ok" : "fail", ok ? "" : " reason=", ok ? "" : reason.c_str());
        if (ok) { im->rung = p.rung; break; }
        last = std::string(phase_rung_name(p.rung)) + " " + reason;
    }
    if (!ok) { *err = "every PHASE rung failed: " + last; return false; }
    im->spatializer = spatializer;
    vos_log("AR_AUDIO spatializer=%s engine=%s", audio_spatializer_name(spatializer), mode_name(im->engine.outputSpatializationMode));
    Impl *ip = im;
    im->interruption = [[NSNotificationCenter defaultCenter] addObserverForName:AVAudioSessionInterruptionNotification object:nil queue:nil
                                                                    usingBlock:^(NSNotification *n) {
        const NSUInteger type = [n.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
        std::lock_guard<std::mutex> g(ip->ctl);
        if (type == AVAudioSessionInterruptionTypeBegan) { ip->interrupted = true; [ip->engine pause]; vos_log("AR_AUDIO phase interrupted"); }
        else {
            ip->interrupted = false;
            if (ip->attached && ip->running && ip->engine) {
                NSError *re = nil;
                if (![[AVAudioSession sharedInstance] setActive:YES error:&re])
                    vos_log("AR_ERROR audio: PHASE reactivating the session: %s", why(re).c_str());
                re = nil;
                if (![ip->engine startAndReturnError:&re]) vos_log("AR_ERROR audio: PHASE restart: %s", why(re).c_str());
            }
            const bool started = ip->engine && ip->engine.renderingState == PHASERenderingStateStarted;
            vos_log("AR_AUDIO phase interruption ended attached=%d running=%d", (int)ip->attached, (int)started);
        }
    }];
    // Headphones or a speaker came or went: PHASE follows the new route itself; if the change stopped the engine while
    // it should play, start it again.
    im->route_change = [[NSNotificationCenter defaultCenter] addObserverForName:AVAudioSessionRouteChangeNotification object:nil queue:nil
                                                                    usingBlock:^(NSNotification *n) {
        const NSUInteger reason = [n.userInfo[AVAudioSessionRouteChangeReasonKey] unsignedIntegerValue];
        std::lock_guard<std::mutex> g(ip->ctl);
        bool restarted = false;
        if (ip->engine && ip->running && ip->attached && !ip->interrupted && ip->engine.renderingState != PHASERenderingStateStarted) {
            NSError *re = nil;
            restarted = [ip->engine startAndReturnError:&re];
            if (!restarted) vos_log("AR_ERROR audio: PHASE restart after a route change: %s", why(re).c_str());
        }
        const bool started = ip->engine && ip->engine.renderingState == PHASERenderingStateStarted;
        vos_log("AR_AUDIO phase route change reason=%lu running=%d%s", (unsigned long)reason, (int)started, restarted ? " (restarted)" : "");
    }];
    return true;
}
bool VosPhase::start_voices(std::string *err) {
    std::lock_guard<std::mutex> g(impl_->ctl);
    if (!impl_->engine || impl_->events.count != AR_AUDIO_MAX_SOURCES + 1) { *err = "the engine or its voices were not set up"; return false; }
    Impl *im = impl_;
    uint32_t i = 0;
    for (PHASESoundEvent *ev in impl_->events) {
        const uint32_t idx = i++;
        // Prepares and starts the event (the render blocks were set before). PHASE reports a failed start only here, and
        // possibly later (asynchronously, maybe inside [engine update]): log it and count it; never take ctl here.
        [ev startWithCompletion:^(PHASESoundEventStartHandlerReason reason) {
            if (reason == PHASESoundEventStartHandlerReasonFailure) {
                im->voices_failed.fetch_add(1, std::memory_order_relaxed);
                vos_log("AR_ERROR audio: PHASE voice %s did not start", idx < AR_AUDIO_MAX_SOURCES ? ("lane" + std::to_string(idx)).c_str() : "bed");
            } else {
                vos_log("AR_AUDIO phase voice %s ended reason=%s", idx < AR_AUDIO_MAX_SOURCES ? ("lane" + std::to_string(idx)).c_str() : "bed",
                        reason == PHASESoundEventStartHandlerReasonTerminated ? "terminated" : "finished");
            }
        }];
    }
    impl_->running = true;
    return true;
}
bool VosPhase::voices_failed() const { return impl_->voices_failed.load(std::memory_order_relaxed) > 0; }
void VosPhase::set_tuning(const ar_audio_tuning &t) {
    impl_->tuning = t;
    ar_audio_tuning_clamp(&impl_->tuning);
    impl_->feed.set_bed_gain(ar_db_to_gain(impl_->tuning.music_db));
}
void VosPhase::set_poly_mode(PolyMode m) { impl_->feed.set_poly_mode(m); }
void VosPhase::set_spatializer(AudioSpatializer m) {
    Impl *im = impl_;
    if (m == im->spatializer) return;
    std::lock_guard<std::mutex> g(im->ctl);
    if (!im->engine) return;
    im->engine.outputSpatializationMode = phase_mode(m);
    im->spatializer = m;
    vos_log("AR_AUDIO spatializer=%s engine=%s (live)", audio_spatializer_name(m), mode_name(im->engine.outputSpatializationMode));
}
void VosPhase::update(const ar_render_frame &rf) {
    Impl *im = impl_;
    if (!im->engine) return;
    ar_audio_tick_meta m;
    while (im->feed.pop_meta(&m)) { im->positions.push(m); im->dropped_sources += m.dropped; }
    ar_audio_source lanes[AR_AUDIO_MAX_SOURCES];
    ar_pose heard_head;
    const bool have = im->positions.at(im->feed.heard_frame(), lanes, &heard_head);
    // The PHASE calls below take ctl like the interruption handler's pause/start (PHASE documents no thread safety for
    // them); ctl is never taken in a render block or a sound-event completion, so this only waits on that handler.
    std::lock_guard<std::mutex> g(im->ctl);
    if (!im->engine) return;
    im->table.transform = to_simd(rf.tracking_from_table);
    im->dbg_table = rf.tracking_from_table;
    im->dbg_listener = ar_pose_mul(rf.tracking_from_table, rf.head_table);
    if (!im->automatic) im->listener.transform = to_simd(im->dbg_listener);
    uint32_t active = 0;
    for (uint32_t l = 0; l < AR_AUDIO_MAX_SOURCES; l++) {
        float gain = 1.f;
        uint32_t id = 0;
        const bool lane_active = have && (lanes[l].flags & AR_SRC_ACTIVE);
        if (lane_active) {
            float pos[3];
            gain = phase_lane_gain(im->tuning, rf.head_table, lanes[l], pos, &im->dbg_level[l]);
            im->sources[l].transform = translation(pos);
            std::memcpy(im->dbg_pos[l], pos, sizeof pos);
            im->dbg_flags[l] = lanes[l].flags;
            id = lanes[l].id;
            active++;
        }
        im->feed.set_lane_gain(l, gain, id != im->gain_id[l]);
        im->gain_id[l] = id;
        im->dbg_active[l] = lane_active;
    }
    im->lanes_active = active;
    [im->engine update];
    if (!im->cadence_logged && im->cadence_n[0].load() >= 8 && im->cadence_n[1].load() >= 8) {
        std::string s[2];
        for (int w = 0; w < 2; w++)
            for (int i = 0; i < 8; i++) {
                char b[48];
                std::snprintf(b, sizeof b, "%s%.0f:%u", i ? "," : "", im->cadence_t[w][i], im->cadence_f[w][i]);
                s[w] += b;
            }
        vos_log("AR_AUDIO phase cadence bed=%s lane0=%s", s[0].c_str(), s[1].c_str());
        im->cadence_logged = true;
    }
}
void VosPhase::pause() {
    std::lock_guard<std::mutex> g(impl_->ctl);
    impl_->attached = false;
    if (impl_->engine) [impl_->engine pause];
}
void VosPhase::resume() {
    std::lock_guard<std::mutex> g(impl_->ctl);
    impl_->attached = true;
    NSError *e = nil;
    if (impl_->engine && !impl_->interrupted && ![impl_->engine startAndReturnError:&e]) vos_log("AR_ERROR audio: PHASE resume: %s", why(e).c_str());
}
bool VosPhase::running() const { std::lock_guard<std::mutex> g(impl_->ctl); return impl_->running && impl_->attached && !impl_->interrupted; }
void VosPhase::stop() { impl_->teardown(); }
const char *VosPhase::render_mode() const { return impl_->client ? "client" : "local"; }
const char *VosPhase::rung() const { return phase_rung_name(impl_->rung); }
PhaseRung VosPhase::started_rung() const { return impl_->rung; }
const char *VosPhase::head_mode() const { return impl_->automatic ? "automatic" : "manual"; }
std::string VosPhase::counters() const {
    const SceneFeed &f = impl_->feed;
    char b[320];
    std::snprintf(b, sizeof b, "frames=%llu cycles=%llu missing=%llu trimmed=%llu busy=%llu mismatched=%llu meta_lost=%llu lanes_active=%u dropped=%llu",
                  (unsigned long long)impl_->out_frames.load(), (unsigned long long)f.cycles(), (unsigned long long)f.missing(),
                  (unsigned long long)f.trimmed(), (unsigned long long)f.busy(), (unsigned long long)f.mismatched(),
                  (unsigned long long)f.meta_lost(), impl_->lanes_active, (unsigned long long)impl_->dropped_sources);
    return b;
}
std::string VosPhase::lanes_debug() const {
    const Impl *im = impl_;
    const ar_mat4 tracking_from_table = ar_mat4_from_pose(im->dbg_table);
    std::string s;
    int shown = 0;
    for (uint32_t l = 0; l < AR_AUDIO_MAX_SOURCES && shown < 4; l++) {
        if (!im->dbg_active[l]) continue;
        float world[4];
        ar_mat4_apply(tracking_from_table, im->dbg_pos[l], world);
        const ListenerDirection d = listener_direction(im->dbg_listener, world);
        char b[96];
        std::snprintf(b, sizeof b, "%slane%u id=%u dist=%.2f az=%.0f el=%.0f", shown ? " " : "", l, im->gain_id[l], d.distance_m,
                      d.azimuth_deg, d.elevation_deg);
        s += b;
        shown++;
    }
    return shown ? s : "none";
}
void VosPhase::sound_points(std::vector<DebugPoint> *out, float size_factor) const {
    out->clear();
    for (uint32_t l = 0; l < AR_AUDIO_MAX_SOURCES; l++) {
        DebugPoint p;
        if (impl_->dbg_active[l] && sound_source_point(impl_->dbg_flags[l], impl_->dbg_pos[l], impl_->dbg_level[l], &p, size_factor))
            out->push_back(p);
    }
}
bool VosPhase::tap_ready() const { return impl_->feed.tap_ready(); }
bool VosPhase::write_tap(const std::string &path) { return impl_->feed.write_tap(path); }
}
