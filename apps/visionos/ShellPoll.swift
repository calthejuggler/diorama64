// A @Published property assigned its own value still sends objectWillChange: every observing view re-renders and an
// open Picker menu loses its scroll. The poll writes only changed values.
import Combine
import Foundation

@MainActor
@discardableResult
func publishIfChanged<Object: AnyObject, Value: Equatable>(_ object: Object, _ key: ReferenceWritableKeyPath<Object, Value>,
                                                           _ value: Value) -> Bool {
    guard object[keyPath: key] != value else { return false }
    object[keyPath: key] = value
    return true
}

@MainActor
final class StatusModel: ObservableObject {
    static let shared = StatusModel()
    @Published private(set) var text = "starting"
    @discardableResult
    func update(_ s: String) -> Bool { publishIfChanged(self, \.text, s) }
}
