import XCTest
import Foundation
import CryptoKit
import Metal
import InfiniteProps

private struct CorpusReader {
    let data: Data
    var at = 0
    mutating func take(_ size: Int) throws -> Data {
        guard size >= 0, at <= data.count - size else { throw CorpusFailure.invalid("Truncated corpus") }
        defer { at += size }
        return data.subdata(in: at..<at + size)
    }
    mutating func word() throws -> UInt32 {
        let b = try take(4)
        return UInt32(b[0]) | UInt32(b[1]) << 8 | UInt32(b[2]) << 16 | UInt32(b[3]) << 24
    }
    mutating func blob() throws -> Data { try take(Int(word())) }
}
private enum CorpusFailure: Error { case invalid(String) }
private func require(_ value: Bool, _ message: String) throws {
    if !value { throw CorpusFailure.invalid(message) }
}

final class PixelCorpusTests: XCTestCase {
    func testEntireSharedCorpusAndAppleImageTransport() throws {
        guard let path = ProcessInfo.processInfo.environment["IPROP_PIXEL_CORPUS_DIR"] else {
            throw CorpusFailure.invalid("Set IPROP_PIXEL_CORPUS_DIR; full pixel validation is required")
        }
        let directory = URL(fileURLWithPath: path)
        let data = try Data(contentsOf: directory.appendingPathComponent("pixel-corpus.bin"))
        let hash = SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
        let manifest = try JSONSerialization.jsonObject(with: Data(contentsOf: directory.appendingPathComponent("pixel-corpus.json"))) as! [String: Any]
        try require(manifest["sha256"] as? String == hash, "Corpus SHA-256 mismatch")
        var reader = CorpusReader(data: data)
        try require(reader.take(8) == Data("IPXCRP01".utf8), "Corpus version")
        var programs = [Data]()
        for _ in 0..<(try reader.word()) { programs.append(try reader.blob()) }
        let count = try reader.word()
        try require(count > 0 && manifest["cases"] as? Int == Int(count), "Incomplete corpus")
        // CPU image transport is mandatory everywhere. Hardware transport is
        // also exercised when the host exposes Metal (hosted CI may not).
        let device = MTLCreateSystemDefaultDevice()
        for index in 0..<count {
            try autoreleasepool {
                let program = try Program(bytes: programs[Int(reader.word())])
                defer { program.close() }
                let seed = try reader.word(), width = try reader.word(), height = try reader.word()
                var overrides = [InputOverride]()
                for _ in 0..<(try reader.word()) {
                    let name = String(decoding: try reader.blob(), as: UTF8.self)
                    let tag = try reader.word(), bits = try reader.word(), integer = try reader.word()
                    let value: InputValue
                    if tag == 0 {
                        value = .color(RGBA8(UInt8(bits & 255), UInt8((bits >> 8) & 255), UInt8((bits >> 16) & 255), UInt8(bits >> 24)))
                    } else {
                        let declaration = program.inputs.first { $0.name.utf8.elementsEqual(name.utf8) }!
                        if case .float = declaration.defaultValue { value = .float(Float(bitPattern: bits)) }
                        else { value = .uint(integer) }
                    }
                    overrides.append(InputOverride(name, value))
                }
                let expectedWidth = try reader.word(), expectedHeight = try reader.word(), expected = try reader.blob()
                let image = try program.generate(seed: seed, width: width, height: height, overrides: overrides)
                let label = "Corpus case \(index), seed \(seed)"
                try require(image.width == expectedWidth && image.height == expectedHeight, label + ": size")
                try require(image.rgba == expected, label + ": RGBA")
                let outputs = image.outputs
                try require(outputs.count == Int(reader.word()), label + ": outputs")
                for output in outputs {
                    try require(Data(output.name.utf8) == reader.blob(), label + ": name")
                    let _ = try reader.word() // Reserved; WASM has no owner IDs.
                    let kind = try reader.word(), list = try reader.word(), elements = try reader.word()
                    try require(output.kind.rawValue == kind && output.isList == (list != 0) && output.count == elements, label + ": slot")
                    try require(output.values.count == Int(reader.word()), label + ": values")
                    for value in output.values { try require(value.bitPattern == reader.word(), label + ": f32 bits") }
                }
                let cg = try image.makeCGImage()
                try require(cg.dataProvider!.data! as Data == expected, label + ": CGImage RGBA")
                if let device = device {
                    let texture = try image.makeMetalTexture(device: device)
                    var bytes = [UInt8](repeating: 0, count: expected.count)
                    texture.getBytes(&bytes, bytesPerRow: Int(image.rowStride),
                        from: MTLRegionMake2D(0, 0, Int(image.width), Int(image.height)), mipmapLevel: 0)
                    try require(Data(bytes) == expected, label + ": Metal RGBA")
                }
            }
        }
        try require(reader.at == data.count, "Unread corpus cases")
        print("Apple + CGImage\(device == nil ? " (Metal unavailable)" : " + Metal"): \(count) pixel-exact cases; SHA-256 \(hash)")
    }
}
