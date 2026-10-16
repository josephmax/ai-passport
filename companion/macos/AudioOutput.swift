import AVFoundation
import AudioToolbox
import CoreAudio
import Darwin

private final class AudioLease {
    private let fd: Int32
    init() throws {
        let dir = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Application Support/Passport Companion", isDirectory: true)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true, attributes: [.posixPermissions: 0o700])
        fd = open(dir.appendingPathComponent("audio.lock").path, O_CREAT | O_RDWR | O_NOFOLLOW, 0o600)
        guard fd >= 0 else { throw Failure("Cannot create audio ownership lock") }
        if flock(fd, LOCK_EX | LOCK_NB) != 0 { close(fd); throw Failure("Another Passport Companion owns audio; quit it before enabling this microphone") }
    }
    deinit { flock(fd, LOCK_UN); close(fd) }
}

final class AudioOutput {
    private let engine: AVAudioEngine
    private let player: AVAudioPlayerNode
    private let format = AVAudioFormat(commonFormat: .pcmFormatFloat32, sampleRate: 16000, channels: 1, interleaved: false)!
    private var queued = 0
    private var generation: UInt64 = 0
    private var order = Wire.AudioOrder()
    private var starting: [Wire.Audio] = []
    private var playing = false
    private var lastFrame = Date.distantPast
    private var primingAt = Date.distantPast
    private var deviceID: AudioDeviceID = 0
    private var lease: AudioLease?

    static func outputs() -> [(AudioDeviceID, String)] {
        var address = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDevices, mScope: kAudioObjectPropertyScopeGlobal, mElement: kAudioObjectPropertyElementMain)
        var size: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject), &address, 0, nil, &size) == noErr else { return [] }
        var ids = [AudioDeviceID](repeating: 0, count: Int(size)/MemoryLayout<AudioDeviceID>.size)
        guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &address, 0, nil, &size, &ids) == noErr else { return [] }
        return ids.compactMap { id in
            var streamsAddress = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyStreams, mScope: kAudioDevicePropertyScopeOutput, mElement: kAudioObjectPropertyElementMain)
            var streamSize: UInt32 = 0
            guard AudioObjectGetPropertyDataSize(id, &streamsAddress, 0, nil, &streamSize) == noErr, streamSize > 0 else { return nil }
            var nameAddress = AudioObjectPropertyAddress(mSelector: kAudioObjectPropertyName, mScope: kAudioObjectPropertyScopeGlobal, mElement: kAudioObjectPropertyElementMain)
            var name: Unmanaged<CFString>?, nameSize = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
            guard AudioObjectGetPropertyData(id, &nameAddress, 0, nil, &nameSize, &name) == noErr, let name else { return nil }
            return (id, name.takeRetainedValue() as String)
        }
    }
    init(name: String) throws {
        guard let (id, _) = Self.outputs().first(where: { $0.1 == name }) else { throw Failure("Audio output '\(name)' missing. Install BlackHole 2ch and select it in Typeless.") }
        lease = try AudioLease()
        engine = AVAudioEngine()
        player = AVAudioPlayerNode()
        guard let unit = engine.outputNode.audioUnit else { throw Failure("No audio output unit") }
        var device = id
        let result = AudioUnitSetProperty(unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &device, UInt32(MemoryLayout.size(ofValue: device)))
        guard result == noErr else { throw Failure("Cannot route audio to '\(name)' (\(result))") }
        deviceID = id
        engine.attach(player)
        engine.connect(player, to: engine.mainMixerNode, format: format)
        engine.prepare(); try engine.start()
        // All queue bookkeeping is on main; realtime completion only dispatches.
    }
    func stop() {
        clearPlayback(); order.reset()
    }
    private func clearPlayback() {
        generation &+= 1; player.stop(); queued = 0; playing = false; starting.removeAll()
    }
    func accept(_ frame: Wire.Audio) throws {
        guard routedToSelectedDevice() else { stop(); throw Failure("Audio output changed; reconnect companion") }
        if try order.accept(frame) {
            clearPlayback(); primingAt = Date()
        }
        lastFrame = Date()
        if !playing {
            starting.append(frame)
            if starting.count < 5 { return } // 100 ms initial jitter buffer.
            for f in starting { try schedule(f.samples) }
            starting.removeAll(); player.play(); playing = true
        } else {
            guard queued > 0 else { throw Failure("Audio buffer underrun; restart microphone") }
            try schedule(frame.samples)
        }
    }
    private func schedule(_ samples: [Float]) throws {
        guard queued < 12 else { throw Failure("Audio buffer overflow; restart microphone") }
        // Correct slow clock drift at the queue edges with a one-sample linear
        // resample. Hard gaps/large congestion still terminate the stream.
        let count = samples.count + (playing && queued <= 2 ? 1 : playing && queued >= 8 ? -1 : 0)
        guard let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: AVAudioFrameCount(count)), let dst = buffer.floatChannelData?[0] else { throw Failure("Audio allocation failed") }
        buffer.frameLength = AVAudioFrameCount(count)
        for i in 0..<count {
            let position = Double(i) * Double(samples.count-1) / Double(count-1)
            let a = Int(position), b = min(samples.count-1, a+1), t = Float(position-Double(a))
            dst[i] = samples[a] + (samples[b]-samples[a])*t
        }
        queued += 1; let current = generation
        player.scheduleBuffer(buffer, completionCallbackType: .dataPlayedBack) { [weak self] _ in
            DispatchQueue.main.async { guard let self, self.generation == current else { return }; self.queued = max(0, self.queued-1) }
        }
    }
    func tick() throws {
        guard order.stream != nil else { return }
        if Date().timeIntervalSince(lastFrame) > 0.5 || (!playing && Date().timeIntervalSince(primingAt) > 0.5) {
            throw Failure("BLE audio stalled; restart microphone")
        }
    }
    private func routedToSelectedDevice() -> Bool {
        guard let unit = engine.outputNode.audioUnit else { return false }
        var current: AudioDeviceID = 0, size = UInt32(MemoryLayout<AudioDeviceID>.size)
        return AudioUnitGetProperty(unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &current, &size) == noErr && current == deviceID
    }
    func finish() { // Drain at most the bounded 240 ms queue, then clear stream state.
        let current = generation
        DispatchQueue.main.asyncAfter(deadline: .now()+0.25) { [weak self] in
            guard let self, self.generation == current else { return }; self.stop()
        }
    }
    func testTone() throws {
        stop()
        for _ in 0..<10 {
            try schedule((0..<320).map { Float(sin(Double($0) * 2 * .pi * 440 / 16000)) * 0.12 })
        }
        player.play(); playing = true
    }
}
