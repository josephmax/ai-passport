import AppKit
import CoreBluetooth
import Foundation

private struct SidecarHTTPError: LocalizedError {
    let status: Int
    var errorDescription: String? { "Sidecar HTTP \(status)" }
}

struct Options {
    var mac: String?
    var scan = false, listAudio = false, testAudio = false
    var service = URL(string: "http://127.0.0.1:3000")!
    var output = "BlackHole 2ch"
    static func validMAC(_ value: String) -> Bool {
        value.range(of: "^([0-9A-F]{2}:){5}[0-9A-F]{2}$", options: .regularExpression) != nil
    }
    init() throws {
        var args = Array(CommandLine.arguments.dropFirst())
        while !args.isEmpty {
            let key = args.removeFirst()
            switch key {
            case "--scan": scan = true
            case "--list-audio": listAudio = true
            case "--test-audio": testAudio = true
            case "--device", "--service", "--audio-output":
                guard !args.isEmpty else { throw Failure("Missing value for \(key)") }
                let value = args.removeFirst()
                if key == "--device" { mac = value.uppercased() }
                if key == "--audio-output" { output = value }
                if key == "--service" {
                    guard let url = URL(string: value), url.scheme == "http", ["127.0.0.1", "localhost", "[::1]", "::1"].contains(url.host ?? ""),
                          url.user == nil, url.password == nil, url.query == nil, url.path.isEmpty || url.path == "/" else { throw Failure("Service must be an HTTP loopback origin") }
                    service = url
                }
            case "--help":
                print("Passport Companion --scan | --device AA:BB:CC:DD:EE:FF [--service http://127.0.0.1:3000] [--audio-output 'BlackHole 2ch']\n--list-audio: enumerate outputs; --test-audio: send a 200ms test tone to the selected output")
                exit(0)
            default: throw Failure("Unknown option: \(key)")
            }
        }
        if let mac, !Self.validMAC(mac) { throw Failure("Device must be the full MAC shown on the pendant") }
        if mac == nil && !scan && !listAudio && !testAudio && !CommandLine.arguments.dropFirst().isEmpty { throw Failure("Specify --scan or --device MAC; connecting never automatically selects a device") }
    }
}

final class Companion: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    private let options: Options
    private var central: CBCentralManager!
    private var peer: CBPeripheral?
    private var chars: [String: CBCharacteristic] = [:]
    private var ignored = Set<UUID>()
    private var candidateCooldown: [UUID: Date] = [:]
    private var generation = UUID()
    private var host = "", device = ""
    private var helloSent = false, helloAcknowledged = false, ready = false, subscribed = false
    private var writing = false, fetching = false, pendingSync = false, mic = false
    private var packets: [Data] = []
    private var lastSync = Date.distantPast, lastActivity = Date(), lastStatus = Date()
    private var syncRequest: UInt32 = 0, accepted: UInt32 = 0
    private var waitingCommit = false, commitDeadline = Date.distantPast
    private var audio: AudioOutput?
    private var timer: Timer?
    private var item: NSStatusItem!
    private var statusText = "Starting"
    private var reconnectAt = Date.distantPast
    private var reconnectAttempt = 0
    private var revoked = false, authorizing = false
    private var lastAuthorization = Date.distantPast
    private var stopped = false

    init(_ options: Options) {
        self.options = options; super.init()
        NSApplication.shared.setActivationPolicy(.accessory)
        item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        let menu = NSMenu()
        menu.addItem(NSMenuItem(title: "Sync now", action: #selector(syncNow), keyEquivalent: "s"))
        menu.addItem(NSMenuItem(title: "Stop microphone", action: #selector(stopMicrophone), keyEquivalent: ""))
        menu.addItem(NSMenuItem(title: "Open configuration", action: #selector(openPortal), keyEquivalent: ""))
        if DesktopApplication.active != nil {
            menu.addItem(NSMenuItem(title: "Companion settings…", action: #selector(desktopSettings), keyEquivalent: ","))
            menu.addItem(NSMenuItem(title: "Copy configuration password", action: #selector(copyPassword), keyEquivalent: ""))
            menu.addItem(NSMenuItem(title: "Open logs", action: #selector(openLogs), keyEquivalent: ""))
        }
        menu.addItem(NSMenuItem(title: "Quit", action: #selector(quit), keyEquivalent: "q"))
        for entry in menu.items { entry.target = self }
        item.menu = menu; status("Starting")
        timer = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { [weak self] _ in self?.tick() }
        if options.scan { startBluetooth(); return }
        fetch(path: "/api/companion") { [weak self] result in
            guard let self, !self.stopped else { return }
            do {
                let json = try result.get()
                guard json["v"] as? Int == 1, let id = json["host"] as? String, UUID(uuidString: id) != nil else { throw Failure("Invalid sidecar identity") }
                self.host = id
                do { self.audio = try AudioOutput(name: self.options.output) }
                catch { self.status("Data only: \(error.localizedDescription)") }
                self.startBluetooth()
            } catch { self.status("Service unavailable: \(error.localizedDescription). Restart companion after service starts.") }
        }
    }
    private func status(_ text: String) {
        guard !stopped else { return }
        if statusText != text { print("[companion] \(text)"); fflush(stdout); statusText = text }
        item.button?.title = mic ? "● Passport" : ready ? "Passport ✓" : "Passport …"
        item.button?.toolTip = text
    }
    @objc private func desktopSettings() { DesktopApplication.active?.showSettings() }
    @objc private func copyPassword() { DesktopApplication.active?.copyPassword() }
    @objc private func openLogs() { DesktopApplication.active?.openLogs() }
    func shutdown() {
        stopped = true
        timer?.invalidate(); timer = nil
        audio?.stop(); audio = nil
        central?.stopScan()
        if let peer { central?.cancelPeripheralConnection(peer) }
        central?.delegate = nil; peer?.delegate = nil
        reset()
        NSStatusBar.system.removeStatusItem(item)
    }
    private func fetch(path: String, completion: @escaping (Result<[String: Any], Error>) -> Void) {
        guard let url = URL(string: path, relativeTo: options.service) else { completion(.failure(Failure("Invalid service URL"))); return }
        var request = URLRequest(url: url); request.timeoutInterval = 5
        URLSession.shared.dataTask(with: request) { data, response, error in
            let result: Result<[String: Any], Error>
            do {
                if let error { throw error }
                guard let response = response as? HTTPURLResponse else { throw Failure("Missing sidecar response") }
                if response.statusCode != 200 { throw SidecarHTTPError(status: response.statusCode) }
                guard let data, data.count < 16384, let json = try JSONSerialization.jsonObject(with: data) as? [String: Any] else { throw Failure("Sidecar response rejected") }
                result = .success(json)
            } catch { result = .failure(error) }
            DispatchQueue.main.async { completion(result) }
        }.resume()
    }
    private func startBluetooth() {
        guard !stopped else { return }
        central = CBCentralManager(delegate: self, queue: .main, options: [CBCentralManagerOptionShowPowerAlertKey: true])
    }
    private func scan() {
        guard central.state == .poweredOn, peer == nil, !revoked else { return }
        central.scanForPeripherals(withServices: [CBUUID(string: Wire.service)], options: nil)
        status("Looking for \(options.mac ?? "Passports"); open Bluetooth pairing on pendant")
    }
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state == .poweredOn { scan() }
        else { reset(); status("Bluetooth unavailable (state \(central.state.rawValue))") }
    }
    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String: Any], rssi RSSI: NSNumber) {
        guard peer == nil, !ignored.contains(peripheral.identifier), Date() >= reconnectAt,
              Date() >= (candidateCooldown[peripheral.identifier] ?? .distantPast) else { return }
        central.stopScan(); peer = peripheral; peripheral.delegate = self; lastActivity = Date()
        central.connect(peripheral, options: nil)
    }
    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        guard peripheral === peer else { central.cancelPeripheralConnection(peripheral); return }
        peripheral.discoverServices([CBUUID(string: Wire.service)])
    }
    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) { disconnected(peripheral, error) }
    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) { disconnected(peripheral, error) }
    private func disconnected(_ peripheral: CBPeripheral, _ error: Error?) {
        guard peripheral === peer else { return }
        // Another unbound pendant may reject us before identity can be read.
        // Give other advertisements a turn instead of repeatedly choosing it.
        if device.isEmpty && !ignored.contains(peripheral.identifier) {
            candidateCooldown[peripheral.identifier] = Date().addingTimeInterval(30)
        }
        reset(); reconnectAttempt += 1
        if revoked { status("Revoked. Allow pairing in portal, restart companion and confirm on pendant."); return }
        reconnectAt = Date().addingTimeInterval(min(30, pow(2, Double(min(5, reconnectAttempt)))))
        status("Disconnected; reconnecting. \(error?.localizedDescription ?? "")")
    }
    private func reset() {
        generation = UUID(); peer = nil; chars.removeAll(); helloSent = false; helloAcknowledged = false
        ready = false; subscribed = false; packets.removeAll(); writing = false; fetching = false
        waitingCommit = false; pendingSync = false; mic = false; device = ""; audio?.stop(); authorizing = false; lastAuthorization = .distantPast
    }
    private func fail(_ text: String) {
        status(text)
        if let peer { central.cancelPeripheralConnection(peer) }
    }
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard peripheral === peer else { return }
        guard error == nil, let service = peripheral.services?.first(where: { $0.uuid == CBUUID(string: Wire.service) }) else { fail("Service discovery failed"); return }
        peripheral.discoverCharacteristics(nil, for: service)
    }
    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        guard peripheral === peer else { return }
        for c in service.characteristics ?? [] { chars[c.uuid.uuidString.lowercased()] = c }
        guard error == nil, let identity = chars[Wire.identity], chars[Wire.control] != nil, chars[Wire.snapshot] != nil, chars[Wire.audio] != nil else { fail("Incomplete BLE protocol"); return }
        peripheral.readValue(for: identity)
    }
    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard peripheral === peer else { return }
        if revoked { return }
        guard error == nil, let data = characteristic.value else { fail(error?.localizedDescription ?? "BLE read failed"); return }
        let key = characteristic.uuid.uuidString.lowercased(); lastActivity = Date()
        if key == Wire.audio {
            guard ready, subscribed else { return }
            do { try audio?.accept(Wire.decode(data)) }
            catch { audio?.stop(); stopMicrophone(); status(error.localizedDescription) }
            return
        }
        guard let json = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any], json["v"] as? Int == 1 else { fail("Unsupported BLE protocol"); return }
        if key == Wire.identity {
            guard let mac = json["device"] as? String, mac.range(of: "^([0-9A-F]{2}:){5}[0-9A-F]{2}$", options: .regularExpression) != nil else { fail("Invalid hardware identity"); return }
            guard json["codec"] as? String == "ima320", json["rate"] as? Int == 16000 else { fail("Unsupported microphone capability"); return }
            if options.scan {
                print("Passport \(mac) (CoreBluetooth \(peripheral.identifier))"); fflush(stdout)
                ignored.insert(peripheral.identifier); central.cancelPeripheralConnection(peripheral); return
            }
            guard mac == options.mac else { ignored.insert(peripheral.identifier); central.cancelPeripheralConnection(peripheral); return }
            device = mac
            // Reading an authenticated characteristic invokes macOS pairing.
            status("Compare Bluetooth number on Mac and pendant, confirm both")
            peripheral.readValue(for: chars[Wire.control]!)
        } else if key == Wire.control {
            lastStatus = Date()
            if !helloSent { sendHello(); return }
            guard helloAcknowledged else { return }
            let isReady = json["ready"] as? Bool == true
            if !isReady { ready = false; status("Confirm this computer/data source on pendant"); return }
            if !ready {
                ready = true; reconnectAttempt = 0; pendingSync = true; lastSync = .distantPast
                peripheral.setNotifyValue(true, for: chars[Wire.control]!)
                if audio != nil { peripheral.setNotifyValue(true, for: chars[Wire.audio]!) }
                status(audio == nil ? "Connected to \(device); data sync only (audio output unavailable)" : "Connected to \(device); select \(options.output) in Typeless")
            }
            let newMic = json["mic"] as? Bool == true
            if mic && !newMic { audio?.finish() }
            mic = newMic
            let request = (json["sync"] as? NSNumber)?.uint32Value ?? 0
            if request != syncRequest { syncRequest = request; pendingSync = true }
            let count = (json["accepted"] as? NSNumber)?.uint32Value ?? 0
            if waitingCommit && count != accepted { waitingCommit = false; lastSync = Date(); status("Snapshot synchronized for \(device)") }
            accepted = count
            if let error = json["error"] as? String, !error.isEmpty {
                waitingCommit = false; packets.removeAll(); pendingSync = true; lastSync = Date()
                status("Pendant: \(error)")
            }
        }
    }
    private func sendHello() {
        guard let peer, let control = chars[Wire.control], !device.isEmpty else { return }
        let rawName = ProcessInfo.processInfo.hostName
        let name = String(String.UnicodeScalarView(rawName.unicodeScalars.filter { $0.value >= 32 && $0.value <= 126 }).prefix(32))
        guard let data = try? JSONSerialization.data(withJSONObject: ["v": 1, "host": host, "name": name.isEmpty ? "Mac" : name]) else { return }
        helloSent = true; peer.writeValue(data, for: control, type: .withResponse)
    }
    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        guard peripheral === peer else { return }
        if revoked { return }
        if let error { fail("BLE write failed: \(error.localizedDescription)"); return }
        lastActivity = Date()
        if characteristic.uuid == CBUUID(string: Wire.control) {
            helloAcknowledged = true; peripheral.readValue(for: characteristic)
        } else if characteristic.uuid == CBUUID(string: Wire.snapshot) {
            writing = false; writeNext()
        }
    }
    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        guard peripheral === peer else { return }
        if characteristic.uuid == CBUUID(string: Wire.audio) {
            subscribed = error == nil && characteristic.isNotifying
            if !subscribed { audio?.stop(); status("Audio notifications unavailable; data sync remains active") }
        }
    }
    @objc private func syncNow() { pendingSync = true; if !mic { lastSync = .distantPast } }
    @objc private func openPortal() { NSWorkspace.shared.open(options.service.appendingPathComponent("portal")) }
    @objc private func stopMicrophone() {
        guard ready, let peer, let control = chars[Wire.control] else { return }
        peer.writeValue(Data("{\"v\":1,\"op\":\"stop\"}".utf8), for: control, type: .withResponse)
    }
    @objc private func quit() { stopMicrophone(); audio?.stop(); if let peer { central.cancelPeripheralConnection(peer) }; NSApplication.shared.terminate(nil) }
    private func syncSnapshot() {
        guard ready, !device.isEmpty, !fetching, !writing, packets.isEmpty, !waitingCommit, !mic else { return }
        fetching = true; pendingSync = false; let epoch = generation, mac = device
        let encoded = mac.addingPercentEncoding(withAllowedCharacters: .urlPathAllowed)!
        fetch(path: "/api/companion/\(encoded)/snapshot?host=\(host)") { [weak self] result in
            guard let self, epoch == self.generation else { return }
            self.fetching = false
            do {
                let json = try result.get()
                guard let source = json["source"] as? [String: Any], source["host"] as? String == self.host,
                      source["device"] as? String == self.device else { throw Failure("Snapshot source mismatch") }
                guard !self.mic else { self.pendingSync = true; return }
                let data = try JSONSerialization.data(withJSONObject: json, options: [.sortedKeys, .withoutEscapingSlashes])
                guard let peer = self.peer else { return }
                self.packets = try Wire.snapshotPackets(data, maximum: min(244, peer.maximumWriteValueLength(for: .withResponse)))
                self.waitingCommit = true; self.commitDeadline = Date().addingTimeInterval(15); self.writeNext()
            } catch {
                if let http = error as? SidecarHTTPError, http.status == 403 { self.revoke(); return }
                self.lastSync = Date(); self.pendingSync = true; self.status(error.localizedDescription)
            }
        }
    }
    private func writeNext() {
        guard !writing, !packets.isEmpty, ready, let peer, let snapshot = chars[Wire.snapshot] else { return }
        if mic { packets.removeAll(); waitingCommit = false; pendingSync = true; return }
        writing = true; peer.writeValue(packets.removeFirst(), for: snapshot, type: .withResponse)
    }
    private func tick() {
        if let central, central.state == .poweredOn, peer == nil, Date() >= reconnectAt, !central.isScanning { scan() }
        if peer != nil && Date().timeIntervalSince(lastActivity) > (ready ? 10 : 100) { fail("BLE session timed out"); return }
        if helloAcknowledged, let peer, let control = chars[Wire.control], Date().timeIntervalSince(lastStatus) > 1 {
            lastStatus = Date(); peer.readValue(for: control)
        }
        if ready && !mic && Date().timeIntervalSince(lastSync) >= (pendingSync ? 5 : 60) { syncSnapshot() }
        if ready && !revoked && !authorizing && Date().timeIntervalSince(lastAuthorization) >= 5 {
            authorizing = true; lastAuthorization = Date(); let epoch = generation
            fetch(path: "/api/companion/\(device)/authorization?host=\(host)") { [weak self] result in
                guard let self, self.generation == epoch else { return }; self.authorizing = false
                if case .failure(let error) = result, let http = error as? SidecarHTTPError, http.status == 403 { self.revoke() }
            }
        }
        if waitingCommit && Date() > commitDeadline {
            packets.removeAll(); waitingCommit = false; pendingSync = true; lastSync = Date(); status("Snapshot acknowledgement timeout")
        }
        if mic { do { try audio?.tick() } catch { audio?.stop(); stopMicrophone(); status(error.localizedDescription) } }
        item.button?.title = mic ? "● Passport" : ready ? "Passport ✓" : "Passport …"
    }
    private func revoke() {
        guard !revoked else { return }
        revoked = true; audio?.stop(); mic = false; ready = false; packets.removeAll(); waitingCommit = false
        if let peer, let control = chars[Wire.control] {
            peer.writeValue(Data("{\"v\":1,\"op\":\"revoke\"}".utf8), for: control, type: .withResponse)
            let epoch = generation
            DispatchQueue.main.asyncAfter(deadline: .now()+0.5) { [weak self] in
                guard let self, self.generation == epoch else { return }; self.central.cancelPeripheralConnection(peer)
            }
        }
        status("Revoked: audio stopped and computer binding removal requested")
    }
}

do {
    let options = try Options()
    if options.listAudio { for (id, name) in AudioOutput.outputs() { print("\(id)\t\(name)") }; exit(0) }
    if options.testAudio {
        let output = try AudioOutput(name: options.output); try output.testTone()
        RunLoop.main.run(until: Date().addingTimeInterval(0.6)); exit(0)
    }
    let app = NSApplication.shared
    if CommandLine.arguments.count == 1 {
        let desktop = try DesktopApplication()
        app.delegate = desktop
        withExtendedLifetime(desktop) { app.run() }
    } else {
        let companion = Companion(options)
        withExtendedLifetime(companion) { app.run() }
    }
} catch {
    fputs("Passport Companion: \(error.localizedDescription)\n", stderr)
    if CommandLine.arguments.count == 1 {
        NSApplication.shared.setActivationPolicy(.accessory)
        NSApplication.shared.activate(ignoringOtherApps: true)
        let alert = NSAlert(); alert.messageText = "Passport Companion"; alert.informativeText = error.localizedDescription
        alert.runModal()
    }
    exit(1)
}
