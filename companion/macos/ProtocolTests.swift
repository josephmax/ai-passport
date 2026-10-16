import Foundation

@main struct ProtocolTests {
    static func main() throws {
        let fixture = try Data(contentsOf: URL(fileURLWithPath: CommandLine.arguments[1]))
        let packet = fixture.prefix(172)
        let frame = try Wire.decode(Data(packet))
        precondition(frame.stream == 0x12345678 && frame.sequence == 42 && frame.samples.count == 320)
        for i in 0..<320 {
            let value = Int16(bitPattern: UInt16(fixture[172+2*i]) | UInt16(fixture[173+2*i]) << 8)
            precondition(frame.samples[i] == Float(value)/32768)
        }
        precondition(Wire.crc32(Data("123456789".utf8)) == 0xcbf43926)
        for length in [1, 19, 20, 244, 4095] {
            let json = Data(repeating: 65, count: length)
            let packets = try Wire.snapshotPackets(json, maximum: 20)
            precondition(packets.first!.count == 7 && packets.last! == Data([3]))
            var restored = Data()
            for data in packets.dropFirst().dropLast() {
                precondition(data.count <= 20 && data[0] == 2)
                precondition(Int(data[1]) | (Int(data[2]) << 8) == restored.count)
                restored.append(data.dropFirst(3))
            }
            precondition(restored == json)
        }
        for invalid in [Data(), Data(repeating: 65, count: 4096), Data([65,0,65])] {
            do { _ = try Wire.snapshotPackets(invalid, maximum: 20); fatalError("Expected rejection") } catch { }
        }
        var bad = Data(packet);bad[11] = 89
        do { _ = try Wire.decode(bad); fatalError("Expected rejection") } catch { }
        var order = Wire.AudioOrder()
        func audio(_ id: UInt32, _ n: UInt32) -> Wire.Audio { .init(stream: id, sequence: n, samples: []) }
        do { _ = try order.accept(audio(1, 2)); fatalError("mid-stream start") } catch { }
        let first = try order.accept(audio(1, 0)); precondition(first)
        let following = try order.accept(audio(1, 1)); precondition(!following)
        do { _ = try order.accept(audio(1, 1)); fatalError("duplicate") } catch { }
        do { _ = try order.accept(audio(1, 3)); fatalError("gap") } catch { }
        order.reset()
        do { _ = try order.accept(audio(1, 0)); fatalError("old session replay after disconnect") } catch { }
        let restarted = try order.accept(audio(2, 0)); precondition(restarted)
        do { _ = try order.accept(audio(1, 0)); fatalError("old stream replaces new one") } catch { }
        print("Swift/C audio interoperability and snapshot framing: PASS")
    }
}
