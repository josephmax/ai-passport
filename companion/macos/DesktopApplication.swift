import AppKit
import Foundation
import Darwin

/// Owns just the embedded child, never an arbitrary service at a known port.
final class DesktopApplication: NSObject, NSApplicationDelegate {
    static weak var active: DesktopApplication?
    private let store: DesktopStore
    private var settings: DesktopSettings?
    private var child: Process?
    private var input: Pipe?
    private var output: Pipe?
    private var log: FileHandle?
    private var startupTimer: Timer?
    private var pending = Data()
    private var serviceURL: URL?
    private var companion: Companion?
    private var item: NSStatusItem!
    private var terminating = false

    init(standalone: Bool = true) throws {
        store = try DesktopStore()
        super.init()
        settings = try store.load()
    }
    func applicationDidFinishLaunching(_ notification: Notification) {
        Self.active = self
        NSApplication.shared.setActivationPolicy(.accessory)
        item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        let menu = NSMenu()
        for (title, action) in [("Companion settings…", #selector(showSettings)), ("Open configuration", #selector(openPortal)),
                                ("Copy configuration password", #selector(copyPassword)), ("Open logs", #selector(openLogs)),
                                ("Restart local service", #selector(restartService)), ("Quit", #selector(quit))] {
            let entry = NSMenuItem(title: title, action: action, keyEquivalent: ""); entry.target = self; menu.addItem(entry)
        }
        item.menu = menu
        startService()
        if settings == nil { DispatchQueue.main.async { self.showSettings() } }
    }
    private func status(_ text: String) {
        item.button?.title = "Passport …"; item.button?.toolTip = text
        print("[desktop] \(text)"); fflush(stdout)
    }
    private func alert(_ text: String) {
        NSApplication.shared.activate(ignoringOtherApps: true)
        let alert = NSAlert(); alert.messageText = "Passport Companion"; alert.informativeText = text; alert.runModal()
    }
    @objc func showSettings() {
        NSApplication.shared.activate(ignoringOtherApps: true)
        let alert = NSAlert(); alert.messageText = "Connect your Passport"
        alert.informativeText = "Enter the full MAC from Settings → Bluetooth on your pendant. Open its pairing window and compare the numbers on both devices. BlackHole 2ch is needed only for voice."
        let view = NSView(frame: NSRect(x: 0, y: 0, width: 360, height: 100))
        let mac = NSTextField(string: settings?.device ?? "")
        mac.placeholderString = "AA:BB:CC:DD:EE:FF"; mac.frame = NSRect(x: 0, y: 62, width: 360, height: 24)
        let label = NSTextField(labelWithString: "Audio output (install separately for dictation)")
        label.frame = NSRect(x: 0, y: 33, width: 360, height: 20)
        let audio = NSTextField(string: settings?.audioOutput ?? "BlackHole 2ch")
        audio.frame = NSRect(x: 0, y: 2, width: 360, height: 24)
        for field in [mac, label, audio] { view.addSubview(field) }
        alert.accessoryView = view; alert.addButton(withTitle: "Save and connect"); alert.addButton(withTitle: "Cancel")
        alert.window.initialFirstResponder = mac
        guard alert.runModal() == .alertFirstButtonReturn else { return }
        do {
            let updated = try DesktopSettings(device: mac.stringValue, audioOutput: audio.stringValue)
            try store.save(updated); settings = updated; connect()
        } catch { self.alert(error.localizedDescription) }
    }
    @objc func copyPassword() {
        do {
            let password = try store.portalPassword()
            NSPasteboard.general.clearContents(); NSPasteboard.general.setString(password, forType: .string)
        } catch { alert(error.localizedDescription) }
    }
    @objc func openLogs() { NSWorkspace.shared.activateFileViewerSelecting([store.logURL]) }
    @objc private func openPortal() {
        guard let serviceURL else { alert("Local service is still starting. Check Open logs if this continues."); return }
        NSWorkspace.shared.open(serviceURL.appendingPathComponent("portal"))
    }
    @objc private func quit() { NSApplication.shared.terminate(nil) }
    @objc private func restartService() {
        // Restart is available only when no child is running: no overlapping writers.
        guard child == nil else { alert("The local service is running. Quit and reopen the app to restart it."); return }
        startService()
    }
    private func connect() {
        guard !terminating, let settings, let serviceURL else { return }
        companion?.shutdown(); companion = nil
        do {
            var options = try Options(); options.mac = settings.device; options.output = settings.audioOutput; options.service = serviceURL
            item.isVisible = false
            companion = Companion(options)
        } catch { item.isVisible = true; alert(error.localizedDescription) }
    }
    private func startService() {
        guard child == nil else { return }
        guard let resources = Bundle.main.resourceURL else { status("App resources unavailable"); return }
        let node = resources.appendingPathComponent("runtime/node")
        let entry = resources.appendingPathComponent("service/tools/desktop-runtime.mjs")
        guard FileManager.default.isExecutableFile(atPath: node.path), FileManager.default.fileExists(atPath: entry.path) else {
            status("Use the packaged desktop app. Developers: run tools/package-companion.sh."); return
        }
        do {
            if FileManager.default.fileExists(atPath: store.logURL.path),
               let size = try FileManager.default.attributesOfItem(atPath: store.logURL.path)[.size] as? NSNumber, size.intValue > 2 * 1024 * 1024 {
                let previous = store.root.appendingPathComponent("service.previous.log")
                if FileManager.default.fileExists(atPath: previous.path) { try FileManager.default.removeItem(at: previous) }
                try FileManager.default.moveItem(at: store.logURL, to: previous)
            }
            if !FileManager.default.fileExists(atPath: store.logURL.path) {
                FileManager.default.createFile(atPath: store.logURL.path, contents: nil, attributes: [.posixPermissions: 0o600])
            }
            log = try FileHandle(forWritingTo: store.logURL); try log?.seekToEnd()
            let process = Process(), stdin = Pipe(), stdout = Pipe()
            process.executableURL = node; process.arguments = [entry.path, store.root.path]
            process.currentDirectoryURL = store.root
            // Finder has no shell setup; child collectors use process.execPath.
            var env = ProcessInfo.processInfo.environment
            for key in Array(env.keys) where key.hasPrefix("NODE_") || key == "DATA_DIR" || key == "PORT" || key == "HOST" || key == "PORTAL_PASSWORD" || key == "SESSION_SECRET" || key.hasPrefix("PASSPORT_") { env.removeValue(forKey: key) }
            env["PATH"] = resources.appendingPathComponent("runtime").path + ":/usr/bin:/bin:/usr/sbin:/sbin"
            process.environment = env; process.standardInput = stdin; process.standardOutput = stdout; process.standardError = stdout
            process.terminationHandler = { [weak self] process in DispatchQueue.main.async { self?.didExit(process) } }
            stdout.fileHandleForReading.readabilityHandler = { [weak self, weak process] handle in
                let data = handle.availableData
                if data.isEmpty { handle.readabilityHandler = nil; return }
                DispatchQueue.main.async {
                    guard let self, let process, self.child === process else { return }
                    self.received(data)
                }
            }
            child = process; input = stdin; output = stdout; pending.removeAll(); serviceURL = nil
            try process.run()
            status("Starting local service…")
            startupTimer = Timer.scheduledTimer(withTimeInterval: 90, repeats: false) { [weak self] _ in
                guard let self, self.serviceURL == nil else { return }
                self.status("Local service startup timed out; open logs, then restart the app.")
                self.stopChild()
            }
        } catch {
            output?.fileHandleForReading.readabilityHandler = nil; child = nil; input = nil; output = nil
            status("Local service failed to start: \(error.localizedDescription)")
        }
    }
    private func received(_ data: Data) {
        try? log?.write(contentsOf: data)
        pending.append(data)
        while let newline = pending.firstIndex(of: 10) {
            let line = pending.subdata(in: 0..<newline); pending.removeSubrange(0...newline)
            let prefix = Data("[companion-ready] ".utf8)
            guard serviceURL == nil, line.starts(with: prefix),
                  let json = try? JSONSerialization.jsonObject(with: line.dropFirst(prefix.count)) as? [String: Any],
                  let port = json["port"] as? Int, (1...65535).contains(port) else { continue }
            serviceURL = URL(string: "http://127.0.0.1:\(port)")
            startupTimer?.invalidate(); startupTimer = nil
            status(settings == nil ? "Local service ready; open Companion settings to select a pendant" : "Connecting…")
            connect()
        }
        if pending.count > 65536 { pending.removeAll() }
    }
    private func didExit(_ process: Process) {
        guard child === process else { return }
        startupTimer?.invalidate(); startupTimer = nil
        companion?.shutdown(); companion = nil; item.isVisible = true
        output?.fileHandleForReading.readabilityHandler = nil
        child = nil; input = nil; output = nil; serviceURL = nil
        try? log?.close(); log = nil
        if terminating { NSApplication.shared.reply(toApplicationShouldTerminate: true) }
        else { status("Local service stopped (\(process.terminationStatus)); open logs or restart local service.") }
    }
    private func stopChild() {
        guard let process = child else { return }
        try? input?.fileHandleForWriting.close()
        if process.isRunning { process.terminate() }
        DispatchQueue.main.asyncAfter(deadline: .now() + 3) { [weak self, weak process] in
            guard let self, let process, self.child === process, process.isRunning else { return }
            kill(process.processIdentifier, SIGKILL)
        }
    }
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        terminating = true; startupTimer?.invalidate(); companion?.shutdown(); companion = nil
        guard child != nil else { return .terminateNow }
        stopChild(); return .terminateLater
    }
}
