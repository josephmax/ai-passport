import Foundation
import Darwin

@main struct DesktopStoreTests {
    static func main() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("passport desktop \(UUID())")
        defer { try? FileManager.default.removeItem(at: root) }
        var store: DesktopStore? = try DesktopStore(root: root)
        let initial = try store!.load(); assert(initial == nil)
        do { _ = try DesktopStore(root: root); fatalError("Two owners accepted") } catch { }
        let settings = try DesktopSettings(device: " aa:bb:cc:dd:ee:ff \n")
        assert(settings.device == "AA:BB:CC:DD:EE:FF")
        try store!.save(settings)
        let saved = try store!.load(); assert(saved == settings)
        let mode = try FileManager.default.attributesOfItem(atPath: store!.settingsURL.path)[.posixPermissions] as! NSNumber
        assert(mode.intValue == 0o600)
        do { _ = try DesktopSettings(device: "AA:BB"); fatalError("Invalid MAC accepted") } catch { }
        try Data("not json".utf8).write(to: store!.settingsURL)
        do { _ = try store!.load(); fatalError("Corrupt state silently reset") } catch { }
        store = nil
        let reopened = try DesktopStore(root: root)
        try reopened.save(settings)
        let restored = try reopened.load(); assert(restored == settings)
        print("Desktop persistence/ownership tests: PASS")
    }
}
