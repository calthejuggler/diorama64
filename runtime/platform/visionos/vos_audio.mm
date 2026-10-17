#import <AVFAudio/AVFAudio.h>
#import <AudioToolbox/AudioToolbox.h>
#include "vos_audio.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>
#include <TargetConditionals.h>
#include "vos_log.h"
#include "vos_latency.h"
#include "vos_resample.h"
#include "vos_scene_audio.h"
#include "wav_io.h"
namespace ar {
struct VosAudio::Impl {
    explicit Impl(ar_runtime *r)
        : rt(r), rs(AR_AUDIO_RATE_HZ, 48000, 4096), bound(kAudioQueueFrames, kAudioStandingFrames, kAudioWindowFrames, pull_game, this) {}
    // A game that booted in sources mode (PHASE's voices failed after the boot) has its sounds in the lanes: they are
    // mixed into the bed. Real-time safe: lanes is preallocated.
    static constexpr uint32_t kMixBlock = 1024;
    static uint32_t pull_game(void *u, int16_t *dst, uint32_t n) {
        Impl *im = static_cast<Impl *>(u);
        if (im->lanes.empty()) return ar_runtime_pull_audio(im->rt, dst, n);
        uint32_t done = 0;
        while (done < n) {
            ar_audio_pull p{};
            p.bed = dst + 2 * (size_t)done;
            p.lanes = im->lanes.data();
            p.frames = std::min<uint32_t>(n - done, kMixBlock);
            const uint32_t got = ar_runtime_pull_audio_scene(im->rt, &p);
            mix_lanes_into_bed(p.bed, im->lanes.data(), got, AR_AUDIO_MAX_SOURCES);
            done += got;
            if (got < p.frames) break;
        }
        return done;
    }
    ar_runtime *rt;
    std::vector<int16_t> lanes;
    StereoResampler rs;
    AudioLatencyBound bound;
    AudioUnit unit = nullptr;
    std::vector<int16_t> tap;                       // preallocated: the callback never allocates
    std::atomic<size_t> tap_pos{0};
    std::atomic<uint64_t> frames{0}, missing{0};
    bool tap_written = false;
    // Control state, under ctl (the layer thread, and AVAudioSession notifications on any thread; never the callback).
    std::mutex ctl;
    bool running = false;
    bool attached = true;
    bool interrupted = false;
    id interruption = nil, route_change = nil;
    void start_unit() { if (unit && !running) running = AudioOutputUnitStart(unit) == noErr; }   // ctl held
    void stop_unit() { if (unit && running) { AudioOutputUnitStop(unit); running = false; } }    // ctl held; waits for
                                                                                                  // the callback to finish
    bool unit_is_running() const {
        UInt32 on = 0, size = sizeof on;
        return unit && AudioUnitGetProperty(unit, kAudioOutputUnitProperty_IsRunning, kAudioUnitScope_Global, 0, &on, &size) == noErr && on;
    }
};
namespace {
// The render callback never blocks: the pull ends in ar_audio_ring_read, which returns 0 frames at once while
// the game thread is writing the ring. The latency bound's queue covers short contention; what it cannot cover, the
// resampler plays as silence and counts as missing, and the bound later drops the delay that silence left.
OSStatus render_cb(void *ctx, AudioUnitRenderActionFlags *, const AudioTimeStamp *, UInt32, UInt32 frames, AudioBufferList *io) {
    auto *im = static_cast<VosAudio::Impl *>(ctx);
    float *out = static_cast<float *>(io->mBuffers[0].mData);
    for (uint32_t done = 0; done < frames;) {
        const uint32_t n = std::min<uint32_t>(frames - done, 4096);
        im->missing += im->rs.render(out + 2 * done, n, AudioLatencyBound::pull, &im->bound);
        done += n;
    }
    im->frames += frames;
    size_t pos = im->tap_pos.load();
    for (uint32_t i = 0; i < 2 * frames && pos < im->tap.size(); i++, pos++)
        im->tap[pos] = (int16_t)std::lround(std::clamp(out[i], -1.f, 1.f) * 32767.f);
    im->tap_pos.store(pos);
    return noErr;
}
}

VosAudio::VosAudio(ar_runtime *rt) : impl_(new Impl(rt)) {}
VosAudio::~VosAudio() { stop(); delete impl_; }
bool VosAudio::start(float tap_s, std::string *err) {
    impl_->tap.assign((size_t)(tap_s * 48000.f) * 2, 0);
    if (ar_runtime_audio_mode(impl_->rt) == AR_AUDIO_MODE_SOURCES) impl_->lanes.assign((size_t)Impl::kMixBlock * AR_AUDIO_MAX_SOURCES, 0);
    NSError *e = nil;
    AVAudioSession *as = [AVAudioSession sharedInstance];
    if (![as setCategory:AVAudioSessionCategoryAmbient error:&e] || ![as setActive:YES error:&e]) {
        *err = std::string("AVAudioSession: ") + (e ? e.localizedDescription.UTF8String : "unknown error");
        return false;
    }
#if TARGET_OS_VISION
    {   // our mix is already head-relative (listener patch + music box); the system must not spatialize it again
        NSError *se = nil;
        const BOOL ok = [as setIntendedSpatialExperience:AVAudioSessionSpatialExperienceBypassed options:@{} error:&se];
        vos_log("AR_AUDIO spatial=bypassed ok=%d%s%s", (int)ok, se ? " error=" : "", se ? se.localizedDescription.UTF8String : "");
    }
#endif
    AudioComponentDescription d = {kAudioUnitType_Output, kAudioUnitSubType_RemoteIO, kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponent c = AudioComponentFindNext(nullptr, &d);
    if (!c || AudioComponentInstanceNew(c, &impl_->unit) != noErr) { impl_->unit = nullptr; *err = "RemoteIO unavailable"; return false; }
    AudioStreamBasicDescription f = {};
    f.mSampleRate = 48000; f.mFormatID = kAudioFormatLinearPCM; f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    f.mBytesPerPacket = 8; f.mFramesPerPacket = 1; f.mBytesPerFrame = 8; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
    AURenderCallbackStruct cb = {render_cb, impl_};
    OSStatus st = AudioUnitSetProperty(impl_->unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
    if (st == noErr) st = AudioUnitSetProperty(impl_->unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
    if (st == noErr) st = AudioUnitInitialize(impl_->unit);
    if (st == noErr) st = AudioOutputUnitStart(impl_->unit);
    if (st != noErr) { stop(); *err = "RemoteIO setup failed, OSStatus " + std::to_string((long)st); return false; }
    {
        std::lock_guard<std::mutex> g(impl_->ctl);
        impl_->running = true;
    }
    Impl *im = impl_;
    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    // Another app's audio, Siri or a call took the session: stop pulling. When it ends, play again only if a layer is
// attached: a closed space keeps the unit stopped as before.
    impl_->interruption = [nc addObserverForName:AVAudioSessionInterruptionNotification object:nil queue:nil usingBlock:^(NSNotification *n) {
        const NSUInteger type = [n.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
        std::lock_guard<std::mutex> g(im->ctl);
        if (type == AVAudioSessionInterruptionTypeBegan) {
            im->interrupted = true;
            im->stop_unit();
            vos_log("AR_AUDIO interrupted");
        } else {
            im->interrupted = false;
            if (im->attached) {
                NSError *ae = nil;
                if (![[AVAudioSession sharedInstance] setActive:YES error:&ae])
                    vos_log("AR_ERROR audio: reactivating the session: %s", ae ? ae.localizedDescription.UTF8String : "unknown error");
                im->start_unit();
            }
            vos_log("AR_AUDIO interruption ended attached=%d running=%d", (int)im->attached, (int)im->running);
        }
    }];
    impl_->route_change = [nc addObserverForName:AVAudioSessionRouteChangeNotification object:nil queue:nil usingBlock:^(NSNotification *n) {
        const NSUInteger reason = [n.userInfo[AVAudioSessionRouteChangeReasonKey] unsignedIntegerValue];
        std::lock_guard<std::mutex> g(im->ctl);
        const bool want = im->attached && !im->interrupted && im->unit;
        bool restarted = false;
        if (want && !im->unit_is_running()) {
            im->running = false;
            im->start_unit();
            restarted = true;
        }
        vos_log("AR_AUDIO route change reason=%lu running=%d%s", (unsigned long)reason, (int)im->running, restarted ? " (restarted)" : "");
    }];
    return true;
}
void VosAudio::pause() {
    std::lock_guard<std::mutex> g(impl_->ctl);
    impl_->attached = false;
    impl_->stop_unit();
}
void VosAudio::resume() {
    std::lock_guard<std::mutex> g(impl_->ctl);
    impl_->attached = true;
    if (!impl_->interrupted) impl_->start_unit();
}
bool VosAudio::running() const { std::lock_guard<std::mutex> g(impl_->ctl); return impl_->running; }
void VosAudio::stop() {
    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    if (impl_->interruption) { [nc removeObserver:impl_->interruption]; impl_->interruption = nil; }
    if (impl_->route_change) { [nc removeObserver:impl_->route_change]; impl_->route_change = nil; }
    std::lock_guard<std::mutex> g(impl_->ctl);
    if (!impl_->unit) return;
    AudioOutputUnitStop(impl_->unit);
    AudioUnitUninitialize(impl_->unit);
    AudioComponentInstanceDispose(impl_->unit);
    impl_->unit = nullptr;
    impl_->running = false;
}
bool VosAudio::mixes_lanes() const { return !impl_->lanes.empty(); }
uint64_t VosAudio::frames_out() const { return impl_->frames.load(); }
uint64_t VosAudio::missing_in() const { return impl_->missing.load(); }
uint64_t VosAudio::trimmed_in() const { return impl_->bound.trimmed(); }
bool VosAudio::tap_ready() const { return !impl_->tap_written && !impl_->tap.empty() && impl_->tap_pos.load() >= impl_->tap.size(); }
bool VosAudio::write_tap(const std::string &path) {
    impl_->tap_written = true;
    return wav_write_s16_stereo(path, impl_->tap.data(), impl_->tap.size() / 2, 48000);
}
}
