import Foundation

enum Wire {
    static let service = "57f10000-ac9d-4b41-8c93-20cc0e04d001"
    static let identity = "57f10001-ac9d-4b41-8c93-20cc0e04d001"
    static let control = "57f10002-ac9d-4b41-8c93-20cc0e04d001"
    static let snapshot = "57f10003-ac9d-4b41-8c93-20cc0e04d001"
    static let audio = "57f10004-ac9d-4b41-8c93-20cc0e04d001"

    static func crc32(_ data: Data) -> UInt32 {
        var c = UInt32.max
        for b in data { c ^= UInt32(b); for _ in 0..<8 { c = (c >> 1) ^ ((c & 1) == 1 ? 0xedb88320 : 0) } }
        return ~c
    }
    static func le(_ value: UInt32, bytes: Int) -> Data {
        Data((0..<bytes).map { UInt8(truncatingIfNeeded: value >> ($0 * 8)) })
    }
    static func snapshotPackets(_ json: Data, maximum: Int) throws -> [Data] {
        guard json.count > 0, json.count < 4096, maximum >= 20, !json.contains(0) else { throw Failure("Snapshot too large or invalid") }
        var packets = [Data([1]) + le(UInt32(json.count), bytes: 2) + le(crc32(json), bytes: 4)]
        var offset = 0
        while offset < json.count {
            let end = min(offset + maximum - 3, json.count)
            packets.append(Data([2]) + le(UInt32(offset), bytes: 2) + json.subdata(in: offset..<end))
            offset = end
        }
        packets.append(Data([3])); return packets
    }
    static let steps = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767]
    static let indices = [-1,-1,-1,-1,2,4,6,8]
    struct Audio {
        let stream: UInt32
        let sequence: UInt32
        let samples: [Float]
    }
    struct AudioOrder {
        private(set) var stream: UInt32?
        private var next: UInt32 = 0
        private var recent: [UInt32] = []
        mutating func reset() { stream = nil; next = 0 } // Retain the bounded replay fence.
        mutating func accept(_ frame: Audio) throws -> Bool {
            if stream != frame.stream {
                guard frame.sequence == 0, !recent.contains(frame.stream) else { throw Failure("Stale or incomplete audio stream; restart microphone") }
                recent.append(frame.stream); if recent.count > 32 { recent.removeFirst() }
                stream = frame.stream; next = 1; return true
            }
            guard frame.sequence == next else { throw Failure("BLE audio packet lost or duplicated; restart microphone") }
            next &+= 1; return false
        }
    }
    static func decode(_ data: Data) throws -> Audio {
        let p = [UInt8](data)
        guard p.count == 172, p[0] == 1, p[11] <= 88 else { throw Failure("Invalid audio frame") }
        func u32(_ i: Int) -> UInt32 { (0..<4).reduce(0) { $0 | (UInt32(p[i+$1]) << ($1*8)) } }
        var predictor = Int(Int16(bitPattern: UInt16(p[9]) | UInt16(p[10]) << 8)), index = Int(p[11])
        var samples = [Float(predictor) / 32768]
        for j in 0..<319 {
            let nib = Int(p[12+j/2] >> ((j%2)*4)) & 15, step = steps[index]
            var diff = step/8
            if nib & 1 != 0 { diff += step/4 }; if nib & 2 != 0 { diff += step/2 }; if nib & 4 != 0 { diff += step }
            predictor = max(-32768, min(32767, predictor + ((nib & 8 != 0) ? -diff : diff)))
            index = max(0, min(88, index + indices[nib & 7]))
            samples.append(Float(predictor) / 32768)
        }
        return Audio(stream: u32(1), sequence: u32(5), samples: samples)
    }
}
struct Failure: LocalizedError {
    let errorDescription: String?
    init(_ text: String) { errorDescription = text }
}
