// Must not be called main.swift: top-level code there collides with @main.
import SwiftUI
import CompositorServices

@MainActor
final class ShellModel: ObservableObject {
    static let shared = ShellModel()
    @Published var spaceOpen = false
    var spaceOpening = false
    // The status line lives in StatusModel (ShellPoll.swift): every view observing this object would re-render with it
    // 4 times a second.
    struct LevelNow: Equatable { var index: Int; var act: Int; var pending: Bool; var name: String }
    @Published var level: LevelNow? = nil
    @Published var levelLabels: [String] = []
    @Published var game = ""
    @Published var sm64AudioState: Int32 = -1
    @Published var hasLevels = false
    func loadGame() {
        var buf = [CChar](repeating: 0, count: 32)
        game = arvos_game(&buf, Int32(buf.count)) != 0 ? String(cString: buf) : ""
        hasLevels = arvos_has_draw_all_rooms() != 0
    }
    func loadSM64World() {
        loadStartIn(); loadDrawAllRooms(); loadHideOutside(); loadOtherRooms(); loadTreeStyle(); loadMarioCm(); loadBillboards(); loadCannonVisible(); SM64LookModel.shared.load()
    }
    static let drawAllRoomsKey = "drawAllRooms"
    @Published var drawAllRooms = true { didSet { if drawAllRooms != oldValue { applyDrawAllRooms() } } }

    func loadDrawAllRooms() {
        let option = arvos_draw_all_rooms_option()
        let saved = UserDefaults.standard.object(forKey: Self.drawAllRoomsKey) as? Bool
        let on = option >= 0 ? option != 0 : (saved ?? true)
        if on == drawAllRooms { applyDrawAllRooms() } else { drawAllRooms = on }
    }

    private func applyDrawAllRooms() {
        if marioCmLock != nil { return }
        UserDefaults.standard.set(drawAllRooms, forKey: Self.drawAllRoomsKey)
        arvos_set_draw_all_rooms(drawAllRooms ? 1 : 0)
    }

    static let hideOutsideKey = "hideOutside"
    @Published var hideOutside = true { didSet { if hideOutside != oldValue { applyHideOutside() } } }

    func loadHideOutside() {
        let option = arvos_hide_outside_option()
        let saved = UserDefaults.standard.object(forKey: Self.hideOutsideKey) as? Bool
        let on = option >= 0 ? option != 0 : (saved ?? true)
        if on == hideOutside { applyHideOutside() } else { hideOutside = on }
    }

    private func applyHideOutside() {
        UserDefaults.standard.set(hideOutside, forKey: Self.hideOutsideKey)
        arvos_set_hide_outside(hideOutside ? 1 : 0)
    }

    static let marioCmKey = "marioCm"
    static let marioCmRange: ClosedRange<Double> = 0.5...10
    @Published var marioCm = 2.0
    @Published var marioCmLock: String? = nil

    func loadMarioCm() {
        let option = arvos_mario_cm_option()
        let saved = UserDefaults.standard.object(forKey: Self.marioCmKey) as? Double
        marioCm = option > 0 ? option : (saved ?? 2.0)
        applyMarioCm(final: true)
    }

    func applyMarioCm(final: Bool) {
        marioCm = min(max(marioCm, Self.marioCmRange.lowerBound), Self.marioCmRange.upperBound)
        if final { UserDefaults.standard.set(marioCm, forKey: Self.marioCmKey) }
        arvos_set_mario_cm(marioCm, final ? 1 : 0)
    }

    func scaleReadout(_ cm: Double) -> String {
        var buf = [CChar](repeating: 0, count: 64)
        let ok = buf.withUnsafeMutableBufferPointer { arvos_scale_readout(cm, $0.baseAddress, Int32($0.count)) }
        return ok != 0 ? String(cString: buf) : String(format: "Mario %.1f cm", cm)
    }

    static let otherRoomsKey = "otherRoomObjects"
    @Published var otherRooms = 2 { didSet { if otherRooms != oldValue { applyOtherRooms() } } }

    func loadOtherRooms() {
        let option = Int(arvos_other_rooms_option())
        let saved = UserDefaults.standard.object(forKey: Self.otherRoomsKey) as? Int
        var mode = option >= 0 ? option : (saved ?? 2)
        if !(0...2).contains(mode) { mode = 2 }
        if mode == otherRooms { applyOtherRooms() } else { otherRooms = mode }
    }

    private func applyOtherRooms() {
        if marioCmLock != nil { return }
        UserDefaults.standard.set(otherRooms, forKey: Self.otherRoomsKey)
        arvos_set_other_rooms(Int32(otherRooms))
    }

    static let treeStyleKey = "treeStyle"
    @Published var treeStyle = 1 { didSet { if treeStyle != oldValue { applyTreeStyle() } } }

    func loadTreeStyle() {
        let option = Int(arvos_tree_style_option())
        let saved = UserDefaults.standard.object(forKey: Self.treeStyleKey) as? Int
        var style = option >= 0 ? option : (saved ?? 1)
        if !(0...1).contains(style) { style = 1 }
        if style == treeStyle { applyTreeStyle() } else { treeStyle = style }
    }

    private func applyTreeStyle() {
        UserDefaults.standard.set(treeStyle, forKey: Self.treeStyleKey)
        arvos_set_tree_style(Int32(treeStyle))
    }

    static let billboardsKey = "billboards"
    @Published var billboards = 1 { didSet { if billboards != oldValue { applyBillboards() } } }

    func loadBillboards() {
        let option = Int(arvos_billboards_option())
        let saved = UserDefaults.standard.object(forKey: Self.billboardsKey) as? Int
        var style = option >= 0 ? option : (saved ?? 1)
        if !(0...1).contains(style) { style = 1 }
        if style == billboards { applyBillboards() } else { billboards = style }
    }

    private func applyBillboards() {
        UserDefaults.standard.set(billboards, forKey: Self.billboardsKey)
        arvos_set_billboards(Int32(billboards))
    }

    static let cannonVisibleKey = "cannonVisible"
    @Published var cannonVisible = true

    func loadCannonVisible() {
        let option = Int(arvos_cannon_visible_option())
        let saved = UserDefaults.standard.object(forKey: Self.cannonVisibleKey) as? Bool
        cannonVisible = option >= 0 ? option != 0 : (saved ?? true)
        applyCannonVisible()
    }

    func applyCannonVisible() {
        UserDefaults.standard.set(cannonVisible, forKey: Self.cannonVisibleKey)
        arvos_set_cannon_visible(cannonVisible ? 1 : 0)
    }

    // Applies at the next launch: the game boots once.
    static let startInKey = "startIn"
    @Published var startStory = true { didSet { if startStory != oldValue && !showingEffectiveStart { applyStartIn() } } }
    @Published var startInNote: String? = nil
    private var showingEffectiveStart = false

    func loadStartIn() {
        let option = Int(arvos_start_option())
        let saved = UserDefaults.standard.object(forKey: Self.startInKey) as? Bool
        let story = option >= 0 ? option != 0 : (saved ?? true)
        if story == startStory { applyStartIn() } else { startStory = story }
        var source = [CChar](repeating: 0, count: 32)
        let effective = Int(arvos_start_effective(&source, Int32(source.count)))
        if effective >= 0 && (effective != 0) != story {
            startInNote = "This launch starts in \(effective != 0 ? "Story" : "Level") (a level=, act= or stars= launch "
                + "argument); your choice \(story ? "Story" : "Level") applies at the next launch."
            arvos_log("AR_SETTING start_in shown=\(effective != 0 ? "story" : "level") saved=\(story ? "story" : "level") source=\(String(cString: source))")
            showingEffectiveStart = true
            startStory = effective != 0
            showingEffectiveStart = false
        }
    }

    private func applyStartIn() {
        startInNote = nil
        if arvos_set_start_in(startStory ? 1 : 0) != 0 { UserDefaults.standard.set(startStory, forKey: Self.startInKey) }
    }

    private var closeSent = true

    func spaceOpened() {
        spaceOpen = true
        closeSent = false
    }

    // The native side is told once per space: a paused layer then ends its frame loop.
    func spaceGone(_ why: String) {
        spaceOpen = false
        guard !closeSent else { return }
        closeSent = true
        arvos_log("AR_SPACE gone (\(why))")
        arvos_space_closed()
    }

    // An open space stays open for the buttons until it is really dismissed: a .background alone (seen during every
    // simulator reopen) must not offer "Open table".
    func spacePhase(_ phase: ScenePhase) {
        arvos_log("AR_SPACE phase=\(phase)")
        if phase == .active && !closeSent { spaceOpen = true }
    }
}

// A saved value is removed (the new default then applies) only if it equals the old default of a change newer than the
// install's PREFIX + "defaults_version", once per change; any other saved value is the user's.
@MainActor
func migrateSavedDefaults(prefix: String, changed: [Int: [String: String]]) {
    let d = UserDefaults.standard
    let have = d.integer(forKey: prefix + "defaults_version")
    let latest = changed.keys.max() ?? 0
    guard have < latest else { return }
    for version in changed.keys.sorted() where version > have {
        for (key, old) in changed[version] ?? [:] where d.string(forKey: prefix + key) == old {
            d.removeObject(forKey: prefix + key)
            arvos_log("AR_SETTING \(prefix)\(key)=\(old) was the old default: now the new one (defaults_version \(version))")
        }
    }
    d.set(latest, forKey: prefix + "defaults_version")
}

@MainActor
final class LookModel: ObservableObject {
    static let shared = LookModel()
    @Published private(set) var values: [String: String] = [:]

    private static func text(_ fill: (UnsafeMutablePointer<CChar>, Int32) -> Int32) -> String? {
        var buf = [CChar](repeating: 0, count: 128)
        let ok = buf.withUnsafeMutableBufferPointer { fill($0.baseAddress!, Int32($0.count)) }
        return ok != 0 ? String(cString: buf) : nil
    }

    func load() {
        let keys = (0..<Int(arvos_look_key_count())).compactMap { i in Self.text({ arvos_look_key(Int32(i), $0, $1) }) }
        values = loadLookValues(UserDefaults.standard, LookLoadBridge(
            keys: keys,
            defaultValue: { key in Self.text({ arvos_look_default(key, $0, $1) }) ?? "" },
            launchValue: { key in Self.text({ arvos_look_option(key, $0, $1) }) },
            apply: { key, value in arvos_set_look(key, value, 1) != 0 },
            dropStored: { key, stored, rev, hasLaunch in arvos_look_drop_stored(key, stored, rev, hasLaunch ? 1 : 0) != 0 },
            defaultsRev: Int32(arvos_look_defaults_rev()),
            log: { arvos_log($0) }))
    }

    func set(_ key: String, _ value: String, final: Bool = true) {
        if final { pendingFinal[key]?.cancel(); pendingFinal[key] = nil }
        guard arvos_set_look(key, value, final ? 1 : 0) != 0 else { return }
        values[key] = value
        if final { UserDefaults.standard.set(value, forKey: "look." + key) }
    }

    // Saved 0.5 s after the last change, so a change without an editing gesture (accessibility adjust, a tap on the
    // track) is saved too.
    func setSliding(_ key: String, _ value: String) {
        set(key, value, final: false)
        pendingFinal[key]?.cancel()
        pendingFinal[key] = Task { @MainActor [weak self] in
            try? await Task.sleep(nanoseconds: 500_000_000)
            guard !Task.isCancelled, let self, let v = self.values[key] else { return }
            self.set(key, v)
        }
    }
    private var pendingFinal: [String: Task<Void, Never>] = [:]

    func flushPending() {
        for key in Array(pendingFinal.keys) { if let v = values[key] { set(key, v) } }
    }

    var roomLit: Bool { values["mode"] == "roomlit" }
    func number(_ key: String) -> Double { Double(values[key] ?? "") ?? 0 }
}

@MainActor
final class AudioModel: ObservableObject {
    static let shared = AudioModel()
    @Published private(set) var values: [String: String] = [:]

    private static func text(_ fill: (UnsafeMutablePointer<CChar>, Int32) -> Int32) -> String? {
        var buf = [CChar](repeating: 0, count: 128)
        let ok = buf.withUnsafeMutableBufferPointer { fill($0.baseAddress!, Int32($0.count)) }
        return ok != 0 ? String(cString: buf) : nil
    }

    private static let changedDefaults: [Int: [String: String]] = [1: ["ring_focus": "1"], 2: ["music": "box"]]

    func load() {
        migrateSavedDefaults(prefix: "audio.", changed: Self.changedDefaults)
        for i in 0..<Int(arvos_audio_key_count()) {
            guard let key = Self.text({ arvos_audio_key(Int32(i), $0, $1) }) else { continue }
            let def = Self.text({ arvos_audio_default(key, $0, $1) }) ?? ""
            var value = Self.text({ arvos_audio_option(key, $0, $1) }) ?? UserDefaults.standard.string(forKey: "audio." + key) ?? def
            if arvos_set_audio(key, value, 1) == 0 { value = def; _ = arvos_set_audio(key, value, 1) }
            UserDefaults.standard.set(value, forKey: "audio." + key)
            values[key] = value
        }
    }

    func set(_ key: String, _ value: String, final: Bool = true) {
        if final { pendingFinal[key]?.cancel(); pendingFinal[key] = nil }
        guard arvos_set_audio(key, value, final ? 1 : 0) != 0 else { return }
        values[key] = value
        if final { UserDefaults.standard.set(value, forKey: "audio." + key) }
    }

    // Saved 0.5 s after the last change, so a change without an editing gesture (accessibility adjust, a tap on the
    // track) is saved too.
    func setSliding(_ key: String, _ value: String) {
        set(key, value, final: false)
        pendingFinal[key]?.cancel()
        pendingFinal[key] = Task { @MainActor [weak self] in
            try? await Task.sleep(nanoseconds: 500_000_000)
            guard !Task.isCancelled, let self, let v = self.values[key] else { return }
            self.set(key, v)
        }
    }
    private var pendingFinal: [String: Task<Void, Never>] = [:]

    func flushPending() {
        for key in Array(pendingFinal.keys) { if let v = values[key] { set(key, v) } }
    }

    func number(_ key: String) -> Double { Double(values[key] ?? "") ?? 0 }

    static let presets: [String] = (0..<Int(arvos_audio_preset_count())).compactMap { i in text({ arvos_audio_preset_name(Int32(i), $0, $1) }) }
    private static func presetValues(_ name: String) -> [(String, String)] {
        (0..<Int(arvos_audio_key_count())).compactMap { i -> (String, String)? in
            guard let key = text({ arvos_audio_key(Int32(i), $0, $1) }),
                  let v = text({ arvos_audio_preset_value(name, key, $0, $1) }) else { return nil }
            return (key, v)
        }
    }
    func applyPreset(_ name: String) {
        guard arvos_apply_audio_preset(name) != 0 else { return }
        for (key, v) in Self.presetValues(name) {
            pendingFinal[key]?.cancel(); pendingFinal[key] = nil
            values[key] = v
            UserDefaults.standard.set(v, forKey: "audio." + key)
        }
    }
    var currentPreset: String? {
        Self.presets.first { p in Self.presetValues(p).allSatisfy { kv in
            values[kv.0] == kv.1 || (Double(values[kv.0] ?? "").map { abs($0 - (Double(kv.1) ?? .nan)) < 1e-6 } ?? false)
        } }
    }
}

struct AudioSlider: View {
    @ObservedObject var audio = AudioModel.shared
    let key: String, label: String, range: ClosedRange<Double>, step: Double, unit: String
    var body: some View {
        HStack {
            Text(label).frame(width: 120, alignment: .leading)
            Slider(value: Binding(get: { audio.number(key) }, set: { audio.setSliding(key, LookSlider.format($0)) }),
                   in: range, step: step,
                   onEditingChanged: { editing in if !editing { audio.set(key, LookSlider.format(audio.number(key))) } })
            Text(LookSlider.format(audio.number(key)) + unit).font(.caption.monospacedDigit()).frame(width: 60, alignment: .trailing)
        }
    }
}

struct AudioSection: View {
    @ObservedObject var audio = AudioModel.shared
    @ObservedObject var shell = ShellModel.shared
    @ObservedObject var sm64 = SM64LookModel.shared
    private var ring: Bool { audio.values["music"] == "ring" }
    var body: some View {
        if shell.game == "sm64" && !sm64.values.isEmpty { SM64SoundRows() }
        focusRows()
    }
    private func focusRows() -> some View {
        let own = "Mario's own sounds"
        return VStack(alignment: .leading, spacing: 10) {
            Text("Audio").font(.headline)
            HStack {
                Text("Preset").frame(width: 120, alignment: .leading)
                ForEach(AudioModel.presets, id: \.self) { p in
                    Button(p.capitalized) { audio.applyPreset(p) }
                        .buttonStyle(.bordered)
                        .tint(audio.currentPreset == p ? .accentColor : nil)
                }
                Text(audio.currentPreset.map { $0.capitalized } ?? "Custom").font(.caption).foregroundStyle(.secondary)
            }
            Text("A preset sets the sliders below (head, Mario, object and ring focus, spotlight, wider, effects and music); change any of them afterwards.")
                .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            AudioSlider(key: "head_focus", label: "Head focus", range: 0...1, step: 0.05, unit: "")
            Text("Far from your head: quieter (strong: about 16 dB at 1.5 m). \(own) stay as they are.")
                .font(.caption).foregroundStyle(.secondary)
            AudioSlider(key: "player_focus", label: "Mario focus", range: 0...1, step: 0.05, unit: "")
            Text("Far from Mario: quieter (strong: -18 dB from \(String(format: "%.3g", arvos_mario_focus_far_m(shell.marioCm))) m on at this Mario size).")
                .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            AudioSlider(key: "object_focus", label: "Object focus", range: 0...1, step: 0.05, unit: "")
            Text("Object sounds farther from you than the nearest one: quieter (strong: about 18 dB at twice the distance). \(own) and jingles stay as they are.")
                .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            AudioSlider(key: "spotlight", label: "Spotlight", range: -30...0, step: 1, unit: " dB")
            AudioSlider(key: "spotlight_cone", label: "Spotlight cone", range: 5...90, step: 1, unit: "°")
            Text("Sounds outside the cone ahead of your head: quieter, down to this level (0 dB: off). \(own) and the music stay as they are.")
                .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            AudioSlider(key: "widen", label: "Wider left/right", range: 1...2, step: 0.05, unit: "×")
            Text("Sounds to the side move further round (1: off). Sounds then no longer come exactly from their objects.")
                .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            AudioSlider(key: "sfx_db", label: "Effects", range: -12...6, step: 0.5, unit: " dB")
            AudioSlider(key: "music_db", label: "Music", range: -24...6, step: 0.5, unit: " dB")
            Divider()
            Picker("Music", selection: Binding(get: { audio.values["music"] ?? "ring" }, set: { audio.set("music", $0) })) {
                Text("Music box").tag("box")
                Text("Orchestra ring").tag("ring")
            }
            .pickerStyle(.segmented)
            AudioSlider(key: "ring_radius", label: "Ring size", range: 0.5...2, step: 0.05, unit: "×").disabled(!ring)
            AudioSlider(key: "ring_rotate", label: "Ring rotation", range: 0...30, step: 1, unit: "°/s").disabled(!ring)
            Picker("Ring height", selection: Binding(get: { audio.values["ring_height"] ?? "head" }, set: { audio.set("ring_height", $0) })) {
                Text("Head").tag("head")
                Text("Table").tag("table")
            }
            .pickerStyle(.segmented)
            .disabled(!ring)
            AudioSlider(key: "ring_focus", label: "Ring focus", range: 0...1, step: 0.05, unit: "").disabled(!ring)
            Text("Each instrument plays from its own place around the table (needs spatial audio). Head: the ring at your head's height, following you. Ring focus: instruments farther than the nearest one quieter (strong: about 18 dB at twice the distance).")
                .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            Divider()
            Picker("Spatial audio", selection: Binding(get: { audio.values["spatializer"] ?? "binaural" }, set: { audio.set("spatializer", $0) })) {
                Text("Binaural").tag("binaural")
                Text("Automatic").tag("automatic")
                Text("Channel").tag("channel")
            }
            .pickerStyle(.segmented)
            Text("Binaural: each sound from its place, also on the built-in speakers. Automatic: the system picks. Channel: plain panning.")
                .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            Divider()
            Toggle("Show sound sources", isOn: Binding(get: { audio.values["show_sources"] == "1" }, set: { audio.set("show_sources", $0 ? "1" : "0") }))
            Text("Debug: a dot where each sound plays from (cyan: music, yellow: Mario, magenta: other sounds); louder is brighter and larger.")
                .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
        }
    }
}

struct LookSlider: View {
    @ObservedObject var look = LookModel.shared
    let key: String, label: String, range: ClosedRange<Double>, step: Double, unit: String
    var body: some View {
        HStack {
            Text(label).frame(width: 120, alignment: .leading)
            Slider(value: Binding(get: { look.number(key) }, set: { look.setSliding(key, Self.format($0)) }),
                   in: range, step: step,
                   onEditingChanged: { editing in if !editing { look.set(key, Self.format(look.number(key))) } })
            Text(Self.format(look.number(key)) + unit).font(.caption.monospacedDigit()).frame(width: 60, alignment: .trailing)
        }
    }
    static func format(_ v: Double) -> String { String(format: "%g", (v * 1000).rounded() / 1000) }
}

@MainActor
final class PacksModel: ObservableObject {
    static let shared = PacksModel()
    struct Pack: Hashable { let name: String; let kind: Int32 }
    @Published var known = false
    @Published var packs: [Pack] = []
    @Published var texturesOn = true
    @Published var modelsOn = true
    @Published var texturePack = ""
    @Published var modelPack = ""
    @Published var trees3d = false
    static let keys: [Int32: (on: String, name: String)] = [1: ("packTexturesOn", "packTextures"), 2: ("packModelsOn", "packModels")]
    // The default pick's order (sm64_packs_pick): the picker's first entry is the pack the game uses for an unknown name.
    private static let preferred = ["SM64 Reloaded", "Render96 HD", "Render96 Models (Balanced)", "Render96 Models", "SwagSkeleton95"]
    func names(_ kind: Int32) -> [String] { packs.filter { $0.kind == kind }.map(\.name) }
    func refresh() {
        let t3 = arvos_trees_3d() != 0
        publishIfChanged(self, \.trees3d, t3)
        let n = Int(arvos_pack_count())
        guard !known, n >= 0 else { return }
        var list: [Pack] = []
        for i in 0..<n {
            var buf = [CChar](repeating: 0, count: 128)
            let kind = arvos_pack_info(Int32(i), &buf, Int32(buf.count))
            if kind == 1 || kind == 2 { list.append(Pack(name: String(cString: buf), kind: kind)) }
        }
        let rank = { (p: Pack) in Self.preferred.firstIndex(of: p.name) ?? Self.preferred.count }
        packs = list.enumerated().sorted { (rank($0.element), $0.offset) < (rank($1.element), $1.offset) }.map(\.element)
        known = true
        for kind: Int32 in [1, 2] { load(kind) }
    }
    private var polling: Task<Void, Never>?
    func startPolling() {
        guard polling == nil else { return }
        polling = Task { @MainActor [weak self] in
            while !Task.isCancelled, let self, !self.known {
                self.refresh()
                if self.known { break }
                try? await Task.sleep(nanoseconds: 250_000_000)
            }
        }
    }
    private func load(_ kind: Int32) {
        var buf = [CChar](repeating: 0, count: 160)
        var on = true, name = ""
        if arvos_pack_option(kind, &buf, Int32(buf.count)) != 0 {
            let v = String(cString: buf)
            on = v != "original"
            if let c = v.firstIndex(of: ":") { name = String(v[v.index(after: c)...]) }
        } else if let k = Self.keys[kind] {
            on = UserDefaults.standard.object(forKey: k.on) as? Bool ?? true
            name = UserDefaults.standard.string(forKey: k.name) ?? ""
        }
        if kind == 1 { texturesOn = on; texturePack = name } else { modelsOn = on; modelPack = name }
        apply(kind)
    }
    func apply(_ kind: Int32) {
        let on = kind == 1 ? texturesOn : modelsOn
        let name = kind == 1 ? texturePack : modelPack
        if let k = Self.keys[kind] {
            UserDefaults.standard.set(on, forKey: k.on)
            UserDefaults.standard.set(name, forKey: k.name)
        }
        arvos_set_packs(kind, on ? 1 : 0, name)
    }
}

struct PacksGroup: View {
    @ObservedObject var packs = PacksModel.shared
    @ObservedObject var look = LookModel.shared
    var body: some View {
        if packs.known && !packs.packs.isEmpty {
            VStack(alignment: .leading, spacing: 6) {
                Text("Packs").font(.subheadline)
                row(kind: 1, label: "Textures", onLabel: "HD", on: $packs.texturesOn, pack: $packs.texturePack)
                row(kind: 2, label: "Models", onLabel: "3D pack", on: $packs.modelsOn, pack: $packs.modelPack)
                if !look.roomLit {
                    Text("Original N64 draws the original textures and models.")
                        .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                }
            }
            .disabled(!look.roomLit)
        }
    }
    @ViewBuilder private func row(kind: Int32, label: String, onLabel: String, on: Binding<Bool>, pack: Binding<String>) -> some View {
        let names = packs.names(kind)
        if !names.isEmpty {
            Picker(label, selection: Binding(get: { on.wrappedValue }, set: { on.wrappedValue = $0; packs.apply(kind) })) {
                Text("Original").tag(false)
                Text(onLabel).tag(true)
            }
            .pickerStyle(.segmented)
            if names.count > 1 {
                Picker("\(label) pack", selection: Binding(get: { names.contains(pack.wrappedValue) ? pack.wrappedValue : names[0] },
                                                          set: { pack.wrappedValue = $0; packs.apply(kind) })) {
                    ForEach(names, id: \.self) { Text($0).tag($0) }
                }
                .disabled(!on.wrappedValue)
            }
        }
    }
}

struct LookSection: View {
    @ObservedObject var look = LookModel.shared
    @ObservedObject var sm64Look = SM64LookModel.shared
    @ObservedObject var shell = ShellModel.shared
    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Look").font(.headline)
            Picker("Look", selection: Binding(get: { look.values["mode"] ?? "roomlit" }, set: { look.set("mode", $0) })) {
                Text("Original N64").tag("original")
                Text("Room-lit").tag("roomlit")
            }
            .pickerStyle(.segmented)
            if shell.hasLevels { PacksGroup() }
            Picker("Light source", selection: Binding(get: { look.values["source"] ?? "room" }, set: { look.set("source", $0) })) {
                Text("Room (visionOS)").tag("room")
                Text("Fixed key light").tag("fixed")
            }
            .pickerStyle(.segmented)
            .disabled(!look.roomLit)
            Picker("Room light", selection: Binding(get: { look.values["light_response"] ?? "steady" }, set: { look.set("light_response", $0) })) {
                Text("Steady").tag("steady")
                Text("Responsive").tag("responsive")
            }
            .pickerStyle(.segmented)
            .disabled(!look.roomLit || look.values["source"] == "fixed")
            TimelineView(.periodic(from: .now, by: 1)) { _ in
                Text(RoomLightText.current()).font(.caption.monospaced())
            }
            LookSlider(key: "match_brightness", label: "Match brightness", range: 0...1, step: 0.05, unit: "")
                .disabled(!look.roomLit)
            LookSlider(key: "match_colour", label: "Match colour", range: 0...1, step: 0.05, unit: "")
                .disabled(!look.roomLit)
            HStack {
                Button("Set as neutral") {
                    var info = arvos_room_light()
                    if arvos_room_light_get(&info) != 0 && info.abs_lum > 0 { look.set("neutral_lum", String(format: "%g", info.abs_lum)) }
                }
                Button("Reset neutral") { look.set("neutral_lum", "0") }
            }
            .disabled(!look.roomLit)
            LookToggle(key: "contact", label: "Contact shadow on the table").disabled(!look.roomLit)
            LookSlider(key: "contact_strength", label: "Contact strength", range: 0...1, step: 0.05, unit: "")
                .disabled(!look.roomLit || look.values["contact"] != "1")
            Picker("Shadows", selection: Binding(get: { look.values["shadows"] ?? "all" }, set: { look.set("shadows", $0) })) {
                Text("Off").tag("off"); Text("Objects").tag("objects"); Text("Objects + level").tag("all")
            }
            .pickerStyle(.segmented).disabled(!look.roomLit)
            Picker("Blob shadows", selection: Binding(get: { look.values["blob"] ?? "hide" }, set: { look.set("blob", $0) })) {
                Text("Keep blob shadows").tag("keep"); Text("Hide blob shadows").tag("hide")
            }
            .pickerStyle(.segmented).disabled(!look.roomLit || look.values["shadows"] == "off")
            LookSlider(key: "softness", label: "Shadow softness", range: 0...1, step: 0.05, unit: "")
                .disabled(!look.roomLit || look.values["shadows"] == "off")
            LookSlider(key: "table_cast", label: "Shadow on table", range: 0...1, step: 0.05, unit: "")
                .disabled(!look.roomLit || look.values["shadows"] == "off")
            VStack(alignment: .leading, spacing: 6) {
                Text("Detail").font(.subheadline)
                LookToggle(key: "relight", label: "Terrain light from its shape").disabled(!look.roomLit)
                LookSlider(key: "relight_strength", label: "Relight strength", range: 0...1, step: 0.05, unit: "")
                    .disabled(!look.roomLit || look.values["relight"] != "1")
                LookToggle(key: "ao", label: "Ambient occlusion").disabled(!look.roomLit)
                LookSlider(key: "ao_strength", label: "AO strength", range: 0...1, step: 0.05, unit: "")
                    .disabled(!look.roomLit || look.values["ao"] != "1")
                LookToggle(key: "toy", label: "Toy material (painted plastic)").disabled(!look.roomLit)
                LookSlider(key: "gloss", label: "Gloss", range: 0...1, step: 0.05, unit: "")
                    .disabled(!look.roomLit || look.values["toy"] != "1")
                LookSlider(key: "rim", label: "Rim light", range: 0...1, step: 0.05, unit: "")
                    .disabled(!look.roomLit || look.values["toy"] != "1")
                LookToggle(key: "reflections", label: "Room reflections (metal cap)")
                    .disabled(!look.roomLit || look.values["toy"] != "1")
            }
            VStack(alignment: .leading, spacing: 6) {
                Text("Fixed key light (and fallback)").font(.subheadline)
                LookSlider(key: "key_az", label: "Direction", range: 0...360, step: 5, unit: "°")
                LookSlider(key: "key_el", label: "Height", range: 10...90, step: 5, unit: "°")
                LookSlider(key: "key_power", label: "Brightness", range: 0.3...1.5, step: 0.05, unit: "×")
                LookSlider(key: "warmth", label: "Warmth", range: -1...1, step: 0.05, unit: "")
                LookSlider(key: "directional", label: "Directional", range: 0...1, step: 0.05, unit: "")
            }
            .disabled(!look.roomLit)
            VStack(alignment: .leading, spacing: 6) {
                Text("Graphics").font(.subheadline).id("graphics")
                Picker("Anti-aliasing (MSAA)", selection: Binding(get: { look.values["msaa"] ?? "" }, set: { look.set("msaa", $0) })) {
                    Text("Off").tag("1"); Text("2×").tag("2"); Text("4×").tag("4")
                }
                .pickerStyle(.segmented)
                LookToggle(key: "edge_aa", label: "Smooth cut-out edges (with MSAA)")
                    .disabled((look.values["msaa"] ?? "4") == "1")
                // fixed when the table's layer is configured (CompositorServices), so they apply at the next Open table
                LookToggle(key: "foveation", label: "Foveation (sharper centre)")
                Picker("Render quality", selection: Binding(get: { look.values["render_quality"] ?? "0" }, set: { look.set("render_quality", $0) })) {
                    ForEach(RenderQualityChoices.tags(current: look.values["render_quality"] ?? "0"), id: \.self) { tag in
                        Text(tag == "0" ? "System" : tag).tag(tag)
                    }
                }
                .pickerStyle(.segmented)
                Text("Foveation and render quality apply at the next Open table.").font(.caption).foregroundStyle(.secondary)
                Picker("Textures", selection: Binding(get: { look.values["tex_filter"] ?? "smooth" }, set: { look.set("tex_filter", $0) })) {
                    Text("Smooth (mipmaps)").tag("smooth"); Text("Original sampling").tag("vanilla")
                }
                .pickerStyle(.segmented)
                LookToggle(key: "decal_fix", label: "Shadow flicker fix (decal offset)")
                Picker("Colour", selection: Binding(get: { look.values["colour"] ?? "auto" }, set: { look.set("colour", $0) })) {
                    Text("Auto").tag("auto"); Text("Linear").tag("linear"); Text("Raw").tag("raw")
                }
                .pickerStyle(.segmented)
                Text("Textures: smooth stops distant textures shimmering. Shadow fix: the game's shadows stop flickering on slopes. Colour: Auto converts for the headset's display; Raw is the washed-out unconverted output. All live.")
                    .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            }
            if ShellModel.shared.game == "sm64" && !sm64Look.values.isEmpty {
                VStack(alignment: .leading, spacing: 6) {
                    Text("Story and 3D models").font(.subheadline).id("story")
                    Toggle("3D models follow the floor", isOn: sm64Look.toggle("pack_tilt"))
                    Text("Menu size")   // a segmented picker does not show its own label
                    Picker("Menu size", selection: sm64Look.binding("menu_size")) {
                        Text("Normal (50 cm)").tag("normal"); Text("Large (70 cm)").tag("large")
                    }
                    .pickerStyle(.segmented)
                    Toggle("Menu backdrop", isOn: sm64Look.toggle("menu_backdrop"))
                    Toggle("Menu backdrop white (off: dark glass)", isOn: sm64Look.toggle("menu_backdrop_white"))
                        .disabled(sm64Look.values["menu_backdrop"] != "on")
                    Toggle("Peach's letter in front of the castle", isOn: sm64Look.toggle("letter_front"))
                    Toggle("Score floats above the level", isOn: sm64Look.toggle("score_float"))
                    Toggle("Score at a seated height, larger", isOn: sm64Look.toggle("score_seated"))
                        .disabled(sm64Look.values["score_float"] != "on")
                    Toggle("Show Lakitu's view during the intro", isOn: sm64Look.toggle("intro_pip"))
                    Toggle("Lakitu's view rides over Lakitu (small)", isOn: sm64Look.toggle("pip_follow"))
                        .disabled(sm64Look.values["intro_pip"] != "on")
                    Toggle("Title at eye level", isOn: sm64Look.toggle("title_eye_level"))
                    Text("Pinch the head")   // a segmented picker does not show its own label
                    Picker("Pinch the head", selection: sm64Look.binding("title_touch")) {
                        Text("At the face + look").tag("both"); Text("At the face").tag("hands"); Text("Look").tag("pinch")
                    }
                    .pickerStyle(.segmented)
                    Text("Eyes follow")
                    Picker("Eyes follow", selection: sm64Look.binding("title_eyes")) {
                        Text("Finger + you").tag("both"); Text("Finger").tag("finger"); Text("Off").tag("off")
                    }
                    .pickerStyle(.segmented)
                    Toggle("Reveal rooms as you visit", isOn: sm64Look.toggle("room_reveal"))
                    Toggle("GAME OVER lettering (Story)", isOn: sm64Look.toggle("gameover_text"))
                    Toggle("Splash logo stays above the table", isOn: sm64Look.toggle("splash_lift"))
                    Toggle("Title head lit from the front", isOn: Binding(get: { sm64Look.values["title_light"] == "front" },
                                                                           set: { sm64Look.set("title_light", $0 ? "front" : "room") }))
                        .disabled(!look.roomLit)
                }
            }
            if ShellModel.shared.game == "sm64" && !sm64Look.values.isEmpty {
                VStack(alignment: .leading, spacing: 6) {
                    Text("Diorama").font(.subheadline).id("diorama")
                    Toggle("Hide what reaches below the table", isOn: Binding(get: { sm64Look.values["below_table"] == "hide" },
                                                                              set: { sm64Look.set("below_table", $0 ? "hide" : "show") }))
                        .disabled(!look.roomLit)
                    Toggle("Trim the level at its edges (water sheets)", isOn: sm64Look.toggle("trim_level")).disabled(!look.roomLit)
                    Toggle("Water and lava look like liquids", isOn: sm64Look.toggle("liquids")).disabled(!look.roomLit)
                    Toggle("Water ends at the shore", isOn: sm64Look.toggle("water_trim"))
                        .disabled(!look.roomLit || sm64Look.values["liquids"] != "on")
                    Text("Mario behind walls")   // a segmented picker does not show its own label
                    Picker("Mario behind walls", selection: sm64Look.binding("behind_walls")) {
                        Text("Silhouette").tag("silhouette"); Text("Cutaway").tag("cutaway"); Text("Both").tag("both"); Text("Off").tag("off")
                    }
                    .pickerStyle(.segmented)
                    Text("Silhouette: a grey outline of Mario shows through what hides him. Cutaway: walls between you and him are cut away (Room-lit only).")
                        .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                    Toggle("Warps fade and iris on the table", isOn: sm64Look.toggle("transitions")).disabled(!look.roomLit)
                    Toggle("Fades: the level turns see-through (off: white or black)", isOn: Binding(get: { sm64Look.values["trans_fade"] == "clear" },
                                                                                                 set: { sm64Look.set("trans_fade", $0 ? "clear" : "colour") }))
                        .disabled(!look.roomLit || sm64Look.values["transitions"] != "on")
                    Toggle("Sharper textures (anisotropy 16, gamma mips)", isOn: sm64Look.toggle("fine_textures"))
                        .disabled(!look.roomLit || (look.values["tex_filter"] ?? "smooth") != "smooth")
                    Toggle("Dialogs and signs: dark text on a white plate", isOn: sm64Look.toggle("dialog_plate"))
                    Text("Dialog size")
                    Picker("Dialog size", selection: sm64Look.binding("dialog_size")) {
                        Text("Normal").tag("normal"); Text("Large (1.5×)").tag("large")
                    }
                    .pickerStyle(.segmented)
                    Toggle("Act selector: a solid star for the next act", isOn: sm64Look.toggle("act_star_solid"))
                    Text("Room-lit only (except dialogs and the act star): Original N64 draws the level as the N64 did.")
                        .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                }
            }
        }
    }
}

enum RenderQualityChoices {
    static let fixed = ["0", "0.6", "0.8", "0.9", "1"]
    static func tags(current: String) -> [String] { fixed.contains(current) ? fixed : fixed + [current] }
}

struct LookToggle: View {
    @ObservedObject var look = LookModel.shared
    let key: String, label: String
    var body: some View {
        Toggle(label, isOn: Binding(get: { look.values[key] == "1" }, set: { look.set(key, $0 ? "1" : "0") }))
    }
}

enum RoomLightText {
    static func current() -> String {
        var i = arvos_room_light()
        guard arvos_room_light_get(&i) != 0 else { return "room light: –" }
        let src = i.room != 0 ? "room" : "fixed key light"
        return String(format: "%@ · brightness %.2f× · probes %llu", src, i.gain, i.probes)
    }
}

struct TableLayerConfiguration: CompositorLayerConfiguration {
    func makeConfiguration(capabilities: LayerRenderer.Capabilities, configuration: inout LayerRenderer.Configuration) {
        // Ask only for what the capabilities list: an unsupported combination makes openImmersiveSpace fail with no
        // detail.
        let wantsFoveation = arvos_foveation() != 0
        let foveatedLayouts = capabilities.supportsFoveation ? capabilities.supportedLayouts(options: [.foveationEnabled]) : []
        let foveate = wantsFoveation && foveatedLayouts.contains(.layered)
        if !foveate {
            let reason = !wantsFoveation ? "setting" : !capabilities.supportsFoveation ? "unsupported" : "no_layered_layout"
            arvos_log("AR_FOVEATION off reason=\(reason)")
        }
        let layouts = foveate ? foveatedLayouts : capabilities.supportedLayouts(options: [])
        configuration.layout = layouts.contains(.layered) ? .layered : .dedicated
        configuration.isFoveationEnabled = foveate
        // A max render quality where the system has no default one (the 26.2 simulator reports NaN) aborts the app when the
        // layer is created (CompositorServices -[CPImmersiveScene layer]), so the setting is then ignored and logged.
        var quality = 0.0
        let qualitySupported = capabilities.defaultRenderQuality.rawValue.isFinite && capabilities.defaultRenderQuality.rawValue > 0
        if arvos_render_quality(&quality) != 0 {
            if qualitySupported { configuration.maxRenderQuality = LayerRenderer.RenderQuality(Float(quality)) }
            else { arvos_log("AR_CAPS render quality \(String(format: "%g", quality)) ignored: no default render quality here") }
        }
        // bgra8Unorm_srgb halves the bandwidth of the default RGBA16Float and the hardware re-encodes our linear
        // output; without it the float drawable is linearized (vos_color.h).
        let colorFormats: [MTLPixelFormat]
        if #available(visionOS 26.0, *) { colorFormats = capabilities.supportedColorFormats(options: []) }
        else { colorFormats = capabilities.supportedColorFormats }
        if colorFormats.contains(.bgra8Unorm_srgb) { configuration.colorFormat = .bgra8Unorm_srgb }
        arvos_log("AR_CAPS colorFormats=\(colorFormats.map { $0.rawValue }) colorFormat=\(configuration.colorFormat.rawValue)")
        arvos_log("AR_CAPS dedicated=\(layouts.contains(.dedicated)) layered=\(layouts.contains(.layered)) foveation=\(capabilities.supportsFoveation) foveate=\(foveate) defaultRenderQuality=\(capabilities.defaultRenderQuality.rawValue) maxRenderQuality=\(configuration.maxRenderQuality.rawValue)")
        // The crash probe only around the risky call: the system creates the layer with this configuration right after
        // we return.
        arvos_render_probe_begin()
    }
}

struct SM64SoundRows: View {
    @ObservedObject private var m = SM64LookModel.shared
    @ObservedObject private var shell = ShellModel.shared
    @ObservedObject private var audio = AudioModel.shared
    private var stereo: Bool { m.values["sound"] == "stereo" }
    private var soundCaption: String {
        if stereo || shell.sm64AudioState == 1 {
            return "Spatial: each object's sound from its place on the table, the music around it. Stereo: the game's own mix."
        }
        return shell.sm64AudioState < 0 ? "Spatial or Stereo is decided when the table opens."
                                        : "Stereo (this device or run has no spatial audio)."
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Sound").font(.headline).id("sound")
            Picker("Sound", selection: m.binding("sound")) { Text("Spatial").tag("spatial"); Text("Stereo").tag("stereo") }
                .pickerStyle(.segmented)
            Text(soundCaption)
                .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            Text("Game sound")
            Picker("Game sound", selection: m.binding("audio")) { Text("On").tag("on"); Text("Off").tag("off") }.pickerStyle(.segmented)
            Text("Game reverb")
            Picker("Game reverb", selection: m.binding("reverb")) { Text("Neutral").tag("neutral"); Text("Original").tag("original") }
                .pickerStyle(.segmented).disabled(stereo || shell.sm64AudioState == 0)
            Text("Neutral: placed sounds keep the same room echo near or far; Original: far sounds echo more, as the game mixes them.")
                .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            Text("Event music")
            Picker("Event music", selection: m.binding("event_music")) { Text("Around the table").tag("ring"); Text("With the game's mix").tag("bed") }
                .pickerStyle(.segmented).disabled(stereo || shell.sm64AudioState == 0 || audio.values["music"] != "ring")
            Text("The merry-go-round, the Piranha Plant lullaby and the endless stairs join the orchestra ring. Jingles stay with the game's mix.")
                .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            Text("Resampler")
            Picker("Resampler", selection: m.binding("resampler")) { Text("Smooth").tag("sinc"); Text("Linear").tag("linear") }
                .pickerStyle(.segmented).disabled(shell.sm64AudioState == 0)
            Text("Smooth: the game's 32 kHz sound reaches the headset without aliasing (cleaner, brighter highs).")
                .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            Divider()
        }
    }
}
@MainActor
final class SM64LookModel: ObservableObject {
    static let shared = SM64LookModel()
    @Published var values: [String: String] = [:]
    private static func text(_ fill: (UnsafeMutablePointer<CChar>, Int32) -> Int32) -> String? {
        var buf = [CChar](repeating: 0, count: 128)
        let ok = buf.withUnsafeMutableBufferPointer { fill($0.baseAddress!, Int32($0.count)) }
        return ok != 0 ? String(cString: buf) : nil
    }
    func load() {
        var keys: [String] = []
        for i in 0..<Int(arvos_sm64_setting_count()) {
            if let key = Self.text({ arvos_sm64_setting_key(Int32(i), $0, $1) }) { keys.append(key) }
        }
        values = loadLookValues(UserDefaults.standard, LookLoadBridge(
            keys: keys,
            defaultValue: { key in Self.text({ arvos_sm64_setting_default(key, $0, $1) }) ?? "" },
            launchValue: { key in Self.text({ arvos_game_setting_option(key, $0, $1) }) },
            apply: { key, value in arvos_sm64_setting_set(key, value) != 0 },
            dropStored: { key, stored, rev, hasLaunch in arvos_sm64_setting_drop_stored(key, stored, rev, hasLaunch ? 1 : 0) != 0 },
            defaultsRev: Int32(arvos_sm64_settings_revision()),
            log: { arvos_log($0) },
            prefix: "sm64"))
    }
    func set(_ key: String, _ value: String) {
        guard arvos_sm64_setting_set(key, value) != 0 else { return }
        values[key] = value
        UserDefaults.standard.set(value, forKey: "sm64." + key)
    }
    func binding(_ key: String) -> Binding<String> { Binding(get: { self.values[key] ?? "" }, set: { self.set(key, $0) }) }
    func toggle(_ key: String) -> Binding<Bool> { Binding(get: { self.values[key] == "on" }, set: { self.set(key, $0 ? "on" : "off") }) }
}
struct LevelPicker: View {
    @ObservedObject private var model = ShellModel.shared
    @State private var names: [String] = []
    @State private var sections: [Int] = []
    @State private var index = 1
    @State private var act = 0
    @State private var edited = false
    @State private var going: Int? = nil
    @State private var sawPending = false
    @State private var note = ""
    @State private var playNote = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            if !names.isEmpty {
                Picker("Start in", selection: $model.startStory) {
                    Text("Story").tag(true)
                    Text("Level").tag(false)
                }
                .pickerStyle(.segmented)
                .disabled(model.marioCmLock != nil)
                Text(model.marioCmLock.map { "locked: \($0)" } ?? model.startInNote
                     ?? "Story: title, file select and the normal game. Applies at the next launch.")
                    .font(.footnote)
                Button("Play from the start") {
                    playNote = arvos_play_from_start() != 0 ? "to the title…" : "not now (reason in the status above)"
                }
                .disabled(model.marioCmLock != nil)
                if !playNote.isEmpty { Text(playNote).font(.footnote) }
                Picker("Level", selection: Binding(get: { index }, set: { index = $0; edited = true })) {
                    ForEach(entries(in: 0), id: \.self) { i in label(i).tag(i) }
                    Section("Secret & Bowser") {
                        ForEach(entries(in: 1), id: \.self) { i in label(i).tag(i) }
                    }
                }
                .pickerStyle(.menu)
                if hasActs(index) {
                    Stepper(act == 0 ? "Act: first without a star" : "Act \(act)", value: $act, in: 0...6)
                }
                Button("Go") {
                    let sent = hasActs(index) ? act : 0
                    if arvos_select_level(Int32(index), Int32(sent)) != 0 {
                        going = index; sawPending = false; edited = false; note = "warping to \(names[index])…"
                    } else {
                        going = nil; note = "not changed (reason in the status above)"
                    }
                }
                if !note.isEmpty { Text(note).font(.footnote) }
            }
        }
        .onAppear {
            var buf = [CChar](repeating: 0, count: 64)
            names = (0..<Int(arvos_level_count())).map { i in
                _ = arvos_level_name(Int32(i), &buf, Int32(buf.count))
                return String(cString: buf)
            }
            sections = names.indices.map { Int(arvos_level_section(Int32($0))) }
            follow(model.level)
        }
        .onChange(of: model.level) { _, now in follow(now); playNote = "" }
    }

    private func hasActs(_ i: Int) -> Bool { arvos_level_has_acts(Int32(i)) != 0 }
    private func entries(in section: Int) -> [Int] {
        names.indices.filter { (sections.count == names.count ? sections[$0] : 0) == section }
    }
    private func label(_ i: Int) -> Text { Text(model.levelLabels.count == names.count ? model.levelLabels[i] : names[i]) }

    private func follow(_ now: ShellModel.LevelNow?) {
        guard let now else { return }
        if now.pending { sawPending = true; return }
        if let g = going, now.index == g || sawPending {
            going = nil
            note = now.index == g ? "" : "landed elsewhere"
        }
        if !edited && names.indices.contains(now.index) { index = now.index }
    }
}

struct StatusLine: View {
    @ObservedObject private var status = StatusModel.shared
    var body: some View {
        Text(status.text).font(.footnote.monospaced()).lineLimit(4)
            .fixedSize(horizontal: false, vertical: true)
    }
}

struct LevelNowLine: View {
    @ObservedObject private var model = ShellModel.shared
    var body: some View { Text(nowText).font(.footnote) }

    private var nowText: String {
        guard let now = model.level else { return "Now: starting" }
        if now.pending { return "Now: warping\u{2026}" }
        let hasActs = now.index >= 0 && arvos_level_has_acts(Int32(now.index)) != 0
        return "Now: \(now.name)" + (now.act > 0 && hasActs ? ", act \(now.act)" : "")
    }
}

enum StatusTab: String, CaseIterable, Identifiable {
    case level = "Level", look = "Look", audio = "Audio", world = "World"
    var id: String { rawValue }
    static let key = "ui.tab"
    private static var argument: [Substring] {
        let args = CommandLine.arguments
        guard let i = args.firstIndex(of: "--ui-tab"), i + 1 < args.count else { return [] }
        return args[i + 1].split(separator: ":", maxSplits: 1)
    }
    static func initial() -> StatusTab {
        if let name = argument.first, let t = allCases.first(where: { $0.rawValue.lowercased() == name.lowercased() }) { return t }
        return UserDefaults.standard.string(forKey: key).flatMap { StatusTab(rawValue: $0) } ?? .level
    }
    static var scrollAnchor: String? { argument.count == 2 ? String(argument[1]) : nil }
}

struct StatusView: View {
    @ObservedObject private var model = ShellModel.shared
    @ObservedObject private var packs = PacksModel.shared
    @Environment(\.openImmersiveSpace) private var openImmersiveSpace
    @Environment(\.dismissImmersiveSpace) private var dismissImmersiveSpace
    @Environment(\.dismissWindow) private var dismissWindow
    @State private var tab = StatusTab.initial()
    @State private var confirmQuit = false

    private func tabPage<C: View>(@ViewBuilder _ content: () -> C) -> some View {
        let page = content()
        return ScrollViewReader { proxy in
            ScrollView {
                page.padding(.vertical, 12).frame(maxWidth: .infinity, alignment: .leading)
            }
            .task {
                guard let anchor = StatusTab.scrollAnchor else { return }
                try? await Task.sleep(for: .milliseconds(500))
                proxy.scrollTo(anchor, anchor: .top)
            }
        }
    }

    @ViewBuilder private var worldControls: some View {
        VStack(alignment: .leading, spacing: 14) {
            VStack(alignment: .leading, spacing: 6) {
                Text("Mario size")
                Slider(value: Binding(get: { log(model.marioCm) },
                                      set: { model.marioCm = (exp($0) * 10).rounded() / 10; model.applyMarioCm(final: false) }),
                       in: log(ShellModel.marioCmRange.lowerBound)...log(ShellModel.marioCmRange.upperBound),
                       onEditingChanged: { editing in if !editing { model.applyMarioCm(final: true) } })
                    .disabled(model.marioCmLock != nil)
                Text(model.scaleReadout(model.marioCm)).font(.footnote.monospacedDigit())
                if let why = model.marioCmLock {
                    Text("Locked: \(why). Mario size changes the game, so it is part of the recording.")
                        .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                }
            }
            Toggle("Draw all rooms", isOn: $model.drawAllRooms)
                .disabled(model.marioCmLock != nil)
            Toggle("Hide outside while inside", isOn: $model.hideOutside)
                .disabled(!model.drawAllRooms)
            VStack(alignment: .leading, spacing: 6) {
                Text("Objects in other rooms")
                Picker("Objects in other rooms", selection: $model.otherRooms) {
                    Text("Hidden").tag(0)
                    Text("Frozen").tag(1)
                    Text("Live (experimental)").tag(2)
                }
                .pickerStyle(.segmented)
                .disabled(!model.drawAllRooms || model.marioCmLock != nil)
                if let why = model.marioCmLock {
                    Text("Locked: \(why). Draw all rooms and Objects in other rooms change the game (Live runs other rooms), so they are part of the recording.")
                        .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                }
            }
            VStack(alignment: .leading, spacing: 6) {
                Text("Billboards").id("billboards")
                Picker("Billboards", selection: $model.billboards) {
                    Text("Upright").tag(1)
                    Text("Face the head").tag(0)
                }
                .pickerStyle(.segmented)
                Text("Trees (with the original card), star and coin numbers: standing vertical, or tilted toward your head.")
                    .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            }
            VStack(alignment: .leading, spacing: 6) {
                Toggle("Cannon visible while aiming", isOn: Binding(get: { model.cannonVisible },
                                                                    set: { model.cannonVisible = $0; model.applyCannonVisible() }))
                    .disabled(model.marioCmLock != nil)
                Text(model.marioCmLock.map { "Locked: \($0). The cannon setting changes the game, so it is part of the recording." }
                     ?? "On: the cannon stays drawn and its barrel follows your aim. Off: hidden while aiming, as in the original.")
                    .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            }
            VStack(alignment: .leading, spacing: 6) {
                Text("Tree style")
                Picker("Tree style", selection: $model.treeStyle) {
                    Text("Original card").tag(0)
                    Text("Crossed cards").tag(1)
                }
                .pickerStyle(.segmented)
                .disabled(packs.trees3d)
                if packs.trees3d {
                    Text("3D trees from the model pack: Tree style does not apply.")
                        .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                }
            }
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Diorama64").font(.title2)
            LevelNowLine()
            StatusLine()
            HStack {
                Button(model.spaceOpen ? "Close table" : "Open table") {
                    Task { if model.spaceOpen { await close() } else { await open() } }
                }
                if model.spaceOpen {
                    Button("Re-place table") { arvos_replace_table() }
                }
                Spacer()
                Button("Quit", role: .destructive) { confirmQuit = true }
            }
            .alert("Quit Diorama64?", isPresented: $confirmQuit) {
                Button("Quit", role: .destructive) { Task { await quit() } }
                Button("Cancel", role: .cancel) {}
            } message: {
                Text("Reopen it from the home view to start fresh.")
            }
            TabView(selection: $tab) {
                tabPage { VStack(alignment: .leading) { LevelPicker() } }.tabItem { Label("Level", systemImage: "map") }.tag(StatusTab.level)
                tabPage { LookSection() }.tabItem { Label("Look", systemImage: "sun.max") }.tag(StatusTab.look)
                tabPage { AudioSection() }.tabItem { Label("Audio", systemImage: "speaker.wave.2") }.tag(StatusTab.audio)
                if model.hasLevels {
                    tabPage { worldControls }.tabItem { Label("World", systemImage: "globe") }.tag(StatusTab.world)
                }
            }
        }
        .padding(24)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .onChange(of: tab) { _, t in UserDefaults.standard.set(t.rawValue, forKey: StatusTab.key) }
        .task { await pollStatus() }
        .task {
            guard CommandLine.arguments.contains("--ui-publish-stats") else { return }
            var shell = 0, status = 0
            let a = model.objectWillChange.sink { _ in shell += 1 }
            let b = StatusModel.shared.objectWillChange.sink { _ in status += 1 }
            while !Task.isCancelled {
                try? await Task.sleep(for: .seconds(10))
                arvos_log("AR_UI publishes in 10 s: shell=\(shell) status=\(status)")
                shell = 0; status = 0
            }
            withExtendedLifetime((a, b)) {}
        }
        .task {
            if arvos_autostart() != 0 { await open() }
            var replaceAfter = 0.0
            if arvos_replace_test(&replaceAfter) != 0 {
                try? await Task.sleep(for: .seconds(replaceAfter))
                if model.spaceOpen { arvos_replace_table() }
            }
            var levelAfter = 0.0, levelIndex: Int32 = 0, levelAct: Int32 = 0
            if arvos_level_test(&levelAfter, &levelIndex, &levelAct) != 0 {
                try? await Task.sleep(for: .seconds(levelAfter))
                _ = arvos_select_level(levelIndex, levelAct)
            }
            var closeAfter = 0.0, reopenAfter = 0.0
            if arvos_lifecycle_test(&closeAfter, &reopenAfter) != 0 {
                try? await Task.sleep(for: .seconds(closeAfter))
                await close()
                try? await Task.sleep(for: .seconds(reopenAfter))
                await open()
            }
        }
    }

    private func open() async {
        // Idempotent: a second call while the space is open or opening (a re-run .task, a double tap) makes
        // openImmersiveSpace return .error.
        guard !model.spaceOpen && !model.spaceOpening else {
            arvos_log("AR_SPACE open skipped (\(model.spaceOpen ? "already open" : "opening"))")
            return
        }
        model.spaceOpening = true
        defer { model.spaceOpening = false }
        let result = await openImmersiveSpace(id: "table")
        arvos_render_probe_end()
        switch result {
        case .opened:
            model.spaceOpened()
            arvos_log("AR_SPACE opened")
            // Not with --lifecycle-test: its close/reopen driver is this view's .task, which dismissing the window
            // would cancel.
            var closeAfter = 0.0, reopenAfter = 0.0
            if arvos_hide_status() != 0 && arvos_lifecycle_test(&closeAfter, &reopenAfter) == 0 {
                dismissWindow(id: "status")
                arvos_log("AR_WINDOW status dismissed (--hold-ticks or --hide-status)")
            }
        case .userCancelled: arvos_log("AR_SPACE cancelled")
        case .error: arvos_log("AR_ERROR openImmersiveSpace returned .error")
        @unknown default: arvos_log("AR_ERROR openImmersiveSpace: unknown result")
        }
    }

    private func quit() async {
        arvos_log("AR_APP quit requested")
        if model.spaceOpen { await close() }
        LookModel.shared.flushPending()
        AudioModel.shared.flushPending()
        _ = arvos_prepare_quit(3.0)
        UserDefaults.standard.synchronize()
        arvos_quit_now()   // _exit: no static destructors while the audio, Metal and game threads still run
    }

    private func close() async {
        await dismissImmersiveSpace()
        model.spaceGone("close button")
    }

    // Every write publishes only a change: an unchanged poll re-renders nothing, so an open menu keeps its scroll.
    private func pollStatus() async {
        var buf = [CChar](repeating: 0, count: 512)
        while !Task.isCancelled {
            _ = arvos_status(&buf, Int32(buf.count))
            StatusModel.shared.update(String(cString: buf))
            var v = arvos_level_view()
            var now: ShellModel.LevelNow? = nil
            var labels: [String] = []
            if arvos_level_view_get(&v) != 0 {
                let name = withUnsafeBytes(of: v.current_name) { String(cString: $0.bindMemory(to: CChar.self).baseAddress!) }
                now = ShellModel.LevelNow(index: Int(v.current), act: Int(v.act), pending: v.pending != 0, name: name)
                var lb = [CChar](repeating: 0, count: 96)
                labels = (0..<Int(arvos_level_count())).map { i in
                    _ = arvos_level_label(Int32(i), &lb, Int32(lb.count))
                    return String(cString: lb)
                }
            }
            PacksModel.shared.refresh()
            var lockBuf = [CChar](repeating: 0, count: 96)
            let lock: String? = arvos_mario_cm_lock(&lockBuf, Int32(lockBuf.count)) != 0 ? String(cString: lockBuf) : nil
            publishIfChanged(model, \.marioCmLock, lock)
            if lock != nil {
                let cm = arvos_mario_cm()
                if cm > 0 { publishIfChanged(model, \.marioCm, cm) }
                let cannon = arvos_cannon_visible()
                if cannon >= 0 { publishIfChanged(model, \.cannonVisible, cannon != 0) }
                let rooms = arvos_draw_all_rooms()
                if rooms >= 0 { publishIfChanged(model, \.drawAllRooms, rooms != 0) }
                let other = Int(arvos_other_rooms())
                if other >= 0 { publishIfChanged(model, \.otherRooms, other) }
            }
            publishIfChanged(model, \.level, now)
            publishIfChanged(model, \.levelLabels, labels)
            let sm64Audio = arvos_sm64_audio_state()
            if model.sm64AudioState != sm64Audio { model.sm64AudioState = sm64Audio }
            publishIfChanged(model, \.hasLevels, arvos_has_draw_all_rooms() != 0)
            try? await Task.sleep(for: .milliseconds(250))
        }
    }
}

struct TableSpace: CompositorContent {
    @Environment(\.scenePhase) private var phase

    var body: some CompositorContent {
        CompositorLayer(configuration: TableLayerConfiguration()) { layerRenderer in
            arvos_render_probe_end()
            // Pinches arrive here on the main thread; the layer thread drains them each frame. Set before the
// frame loop starts so the first pinch is not missed.
            layerRenderer.onSpatialEvent = { events in
                for e in events {
                    var c = arvos_spatial_event()
                    c.id = UInt64(UInt(bitPattern: e.id.hashValue))
                    switch e.kind {
                    case .indirectPinch: c.kind = 0
                    case .directPinch: c.kind = 1
                    case .pointer: c.kind = 2
                    case .touch: c.kind = 3
                    @unknown default: c.kind = 4
                    }
                    switch e.phase {
                    case .active: c.phase = 0
                    case .ended: c.phase = 1
                    case .cancelled: c.phase = 2
                    @unknown default: c.phase = 2
                    }
                    c.timestamp = e.timestamp
                    if let r = e.selectionRay {
                        c.has_ray = 1
                        c.ray_origin = (Float(r.origin.x), Float(r.origin.y), Float(r.origin.z))
                        c.ray_direction = (Float(r.direction.x), Float(r.direction.y), Float(r.direction.z))
                    }
                    if let p = e.inputDevicePose {
                        c.has_pose = 1
                        c.pose_position = (Float(p.pose3D.position.x), Float(p.pose3D.position.y), Float(p.pose3D.position.z))
                        let q = p.pose3D.rotation.quaternion
                        c.pose_orientation = (Float(q.imag.x), Float(q.imag.y), Float(q.imag.z), Float(q.real))
                    }
                    switch e.chirality { case .some(.left): c.chirality = 1; case .some(.right): c.chirality = 2; default: c.chirality = 0 }
                    arvos_push_spatial_event(&c)
                }
            }
            // This closure runs on the main thread and the frame loop blocks: give it its own thread.
            let thread = Thread { arvos_layer_run(Unmanaged.passUnretained(layerRenderer).toOpaque()) }
            thread.name = "sm64ar-render"
            thread.stackSize = 4 << 20
            thread.start()
        }
        .onChange(of: phase) { _, now in ShellModel.shared.spacePhase(now) }
        .onDisappear { ShellModel.shared.spaceGone("disappeared") }
    }
}

@main
struct SM64ARApp: App {
    @Environment(\.scenePhase) private var scenePhase

    init() {
        let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].path
        let owned: [UnsafeMutablePointer<CChar>?] = CommandLine.arguments.map { strdup($0) }
        defer { owned.forEach { free($0) } }
        let argv: [UnsafePointer<CChar>?] = owned.map { $0.map { UnsafePointer($0) } }
        let ok = argv.withUnsafeBufferPointer { arvos_configure(Int32($0.count), $0.baseAddress, Bundle.main.bundlePath, docs) }
        if ok == 0 { NSLog("arvos_configure failed") }
        ShellModel.shared.loadGame()
        ShellModel.shared.loadSM64World()
        LookModel.shared.load()
        PacksModel.shared.startPolling()
        AudioModel.shared.load()
    }

    var body: some Scene {
        WindowGroup(id: "status") { StatusView() }
            .defaultSize(width: 640, height: 640)
        ImmersiveSpace(id: "table") { TableSpace() }
        .immersionStyle(selection: .constant(.mixed), in: .mixed)
        .onChange(of: scenePhase) { _, phase in arvos_set_background(phase == .background ? 1 : 0) }
        .upperLimbVisibility(.automatic)
    }
}
