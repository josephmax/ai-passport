import Foundation
import Darwin

struct DesktopSettings: Codable, Equatable {
    var device: String
    var audioOutput: String

    init(device: String, audioOutput: String = "BlackHole 2ch") throws {
        let mac = device.trimmingCharacters(in: .whitespacesAndNewlines).uppercased()
        guard mac.range(of: "^([0-9A-F]{2}:){5}[0-9A-F]{2}$", options: .regularExpression) != nil,
              !audioOutput.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty,
              audioOutput.utf8.count <= 256 else { throw Failure("Enter the full MAC shown on the pendant and an audio output name.") }
        self.device = mac; self.audioOutput = audioOutput
    }

    func validated() throws -> DesktopSettings { try DesktopSettings(device: device, audioOutput: audioOutput) }
}

/// User state lives outside the signed bundle and survives replacement/moving.
final class DesktopStore {
    let root: URL
    private var lockFD: Int32 = -1
    var settingsURL: URL { root.appendingPathComponent("desktop.json") }
    var logURL: URL { root.appendingPathComponent("service.log") }
    var envURL: URL { root.appendingPathComponent(".env") }

    init(root: URL = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Application Support/Passport Companion", isDirectory: true)) throws {
        self.root = root
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true, attributes: [.posixPermissions: 0o700])
        lockFD = open(root.appendingPathComponent("desktop.lock").path, O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0o600)
        guard lockFD >= 0 else { throw Failure("Cannot open the companion ownership lock.") }
        guard flock(lockFD, LOCK_EX | LOCK_NB) == 0 else {
            close(lockFD); lockFD = -1
            throw Failure("Passport Companion is already running. Use Passport in the menu bar.")
        }
    }
    deinit { if lockFD >= 0 { flock(lockFD, LOCK_UN); close(lockFD) } }

    func load() throws -> DesktopSettings? {
        guard FileManager.default.fileExists(atPath: settingsURL.path) else { return nil }
        return try JSONDecoder().decode(DesktopSettings.self, from: Data(contentsOf: settingsURL)).validated()
    }
    func save(_ settings: DesktopSettings) throws {
        let data = try JSONEncoder().encode(settings.validated())
        try data.write(to: settingsURL, options: .atomic)
        try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: settingsURL.path)
    }
    func portalPassword() throws -> String {
        let env = try String(contentsOf: envURL, encoding: .utf8)
        guard let line = env.split(separator: "\n").first(where: { $0.hasPrefix("PORTAL_PASSWORD=") }) else { throw Failure("The service has not prepared its password yet.") }
        return String(line.dropFirst("PORTAL_PASSWORD=".count))
    }
}
