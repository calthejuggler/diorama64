// Apart from the C bridge so it can run over UserDefaults(suiteName:) with a fake bridge.
import Foundation

struct LookLoadBridge {
    var keys: [String]
    var defaultValue: (String) -> String
    var launchValue: (String) -> String?
    var apply: (String, String) -> Bool
    var dropStored: (_ key: String, _ stored: String, _ rev: Int32, _ hasLaunch: Bool) -> Bool
    var defaultsRev: Int32
    var log: (String) -> Void
    var prefix = "look"
}

func loadLookValues(_ ud: UserDefaults, _ b: LookLoadBridge) -> [String: String] {
    var values: [String: String] = [:]
    let p = b.prefix + "."
    let rev = Int32(max(ud.integer(forKey: p + "defaults_rev"), ud.integer(forKey: p + "defaults_version")))
    let newRev = b.defaultsRev
    for key in b.keys {
        let def = b.defaultValue(key)
        let launch = b.launchValue(key)
        var stored = ud.string(forKey: p + key)
        if rev < newRev, let old = stored, b.dropStored(key, old, rev, launch != nil) {
            ud.removeObject(forKey: p + key)
            b.log("AR_SETTING \(p)\(key) stored \(old) was the old default: dropped (defaults rev \(newRev))")
            stored = nil
        }
        var value = launch ?? stored ?? def
        if !b.apply(key, value) {
            if launch == nil { ud.removeObject(forKey: p + key) }
            value = def; _ = b.apply(key, value)
        } else if launch != nil {
            ud.set(value, forKey: p + key)
        }
        values[key] = value
    }
    if rev < newRev { ud.set(Int(newRev), forKey: p + "defaults_rev") }
    return values
}
