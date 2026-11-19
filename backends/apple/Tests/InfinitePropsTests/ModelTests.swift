#if IPROP_MODELS
import XCTest
import Foundation
import CryptoKit
import InfiniteProps

private extension CorpusReader {
    mutating func wide() throws -> UInt64 { try UInt64(word()) | UInt64(word()) << 32 }
    mutating func text() throws -> String { String(decoding: try blob(), as: UTF8.self) }
}
private func fnv(_ bytes: Data) -> UInt64 {
    bytes.reduce(UInt64(0xcbf29ce484222325)) { ($0 ^ UInt64($1)) &* 0x100000001b3 }
}

final class ModelTests: XCTestCase {
    private var directory: URL {
        URL(fileURLWithPath: ProcessInfo.processInfo.environment["IPROP_FIXTURES"] ?? "build/binding-fixtures")
    }

    func testModelCorpus() throws {
        let data = try Data(contentsOf: directory.appendingPathComponent("model-corpus.bin"))
        let manifest = try JSONSerialization.jsonObject(with: Data(contentsOf: directory.appendingPathComponent("model-corpus.json"))) as! [String: Any]
        XCTAssertEqual(manifest["sha256"] as? String, SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined())
        var r = CorpusReader(data: data)
        XCTAssertEqual(try r.take(8), Data("IPMCRP01".utf8))
        var models: [Model] = []
        for _ in 0..<(try r.word()) { models.append(try Model(bytes: r.blob())) }
        let count = try r.word()
        XCTAssertEqual(Int(count), manifest["cases"] as? Int)
        for _ in 0..<count {
            let label = try r.text(), index = try r.word(), seed = try r.word(), lod = try r.word()
            var inputs: [String: InputValue] = [:]
            for _ in 0..<(try r.word()) {
                let name = try r.text(), kind = try r.word(), bits = try r.word()
                switch kind {
                case 0: inputs[name] = .color(RGBA8(UInt8(truncatingIfNeeded: bits), UInt8(truncatingIfNeeded: bits >> 8),
                                                    UInt8(truncatingIfNeeded: bits >> 16), UInt8(bits >> 24)))
                case 1: inputs[name] = .float(Float(bitPattern: bits))
                default: inputs[name] = .uint(bits)
                }
            }
            let m = try models[Int(index)].generate(seed: seed, inputs: inputs, lod: lod)
            for f in [m.size.x, m.size.y, m.size.z, m.unitsPerMeter, m.boundsMin.x, m.boundsMin.y, m.boundsMin.z,
                      m.boundsMax.x, m.boundsMax.y, m.boundsMax.z] { XCTAssertEqual(f.bitPattern, try r.word(), label) }
            let s = m.statistics
            for n in [s.vertexCount, s.triangleCount] { XCTAssertEqual(n, try r.word(), label) }
            for n in [s.fuelBudget, s.fuelUsed, s.workBudget, s.workUsed] { XCTAssertEqual(n, try r.wide(), label) }
            XCTAssertEqual(m.textures.count, Int(try r.word()), label)
            for t in m.textures {
                XCTAssertEqual(t.key, try r.wide(), label)
                XCTAssertEqual(t.name, try r.text(), label)
                for n in [t.width, t.height, t.assetWidth, t.assetHeight, t.repeats ? 1 : 0] { XCTAssertEqual(n, try r.word(), label) }
                XCTAssertEqual(fnv(t.rgba), try r.wide(), label)
                XCTAssertEqual(try t.makeCGImage().dataProvider!.data! as Data, t.rgba)
                XCTAssertEqual(t.images.count, Int(try r.word()), label)
                for a in t.images {
                    XCTAssertEqual(a.kind.rawValue, try r.word(), label)
                    XCTAssertEqual(a.name, try r.text(), label)
                    XCTAssertEqual(fnv(a.rgba), try r.wide(), label)
                }
            }
            XCTAssertEqual(m.surfaces.count, Int(try r.word()), label)
            for s in m.surfaces {
                for n in [s.textureIndex, s.roughness.bitPattern, s.metallic.bitPattern, s.emission.bitPattern, s.alphaMode,
                          s.alphaCutoff.bitPattern, s.twoSided ? 1 : 0, s.crisp ? 1 : 0] { XCTAssertEqual(n, try r.word(), label) }
                for values in [s.positions, s.normals, s.tangents, s.uvs] {
                    XCTAssertEqual(values.withUnsafeBytes { Data($0) }, try r.blob(), label)
                }
                XCTAssertEqual(s.indices.withUnsafeBytes { Data($0) }, try r.blob(), label)
            }
            XCTAssertEqual(m.outputs.count, Int(try r.word()), label)
            for o in m.outputs {
                XCTAssertEqual(o.name, try r.text(), label)
                for n in [o.kind, o.isList ? 1 : 0, o.valueCount] { XCTAssertEqual(n, try r.word(), label) }
                XCTAssertEqual(o.values.withUnsafeBytes { Data($0) }, try r.blob(), label)
            }
        }
        XCTAssertEqual(r.at, data.count)
    }

    func testDeclarationsSkinAndErrors() throws {
        let model = try Model(contentsOf: directory.appendingPathComponent("model-crate-debug.iprop3d"))
        XCTAssertEqual(model.metadata.size, SIMD3(12,10,8))
        XCTAssertEqual(model.metadata.unitsPerMeter, 50)
        XCTAssertEqual(model.inputs.map { $0.name }, ["wide", "stain", "extra", "rounding"])
        XCTAssertEqual(model.inputs.map { $0.defaultValue }, [.float(8), .color(RGBA8(51,102,153)), .uint(1), .float(0.5)])
        XCTAssertEqual(model.skinPrograms.map { $0.name }, ["tile"])
        let tile = try Program(contentsOf: directory.appendingPathComponent("model-tile.iprop"))
        let texture = try model.generate(seed: 5).textures[0]
        let image = try tile.generate(seed: 77, width: 32, height: 16, inputs: ["tint": .color(RGBA8(51,102,153))])
        XCTAssertEqual(texture.rgba, image.rgba)
        XCTAssertEqual(texture.images, image.images)
        for (inputs, limits, reason) in [
            (["wide": InputValue.float(0)], ExecutionLimits(), UInt32(3010)),
            (["wide": .uint(0)], ExecutionLimits(), 4004),
            (["missing": .uint(0)], ExecutionLimits(), 4005),
            ([:], ExecutionLimits(workLimit: 200), 3015)] {
            XCTAssertThrowsError(try model.generate(inputs: inputs, limits: limits)) { error in
                let failure = error as? IpropError
                XCTAssertEqual(failure?.reason, reason)
                if reason == 3010 { XCTAssertGreaterThan(failure?.debugLine ?? 0, 0) }
                if reason == 3015 { XCTAssertTrue(failure?.message.contains("skin 'tile'") == true) }
            }
            _ = try model.generate()
        }
    }

    func testModelLifetimeAndCache() throws {
        let m = try Model(contentsOf: directory.appendingPathComponent("model-crate.iprop3d"))
        let cache = TextureCache()
        let cold = try m.generate(textures: cache)
        let hot = try m.generate(textures: cache)
        XCTAssertTrue(hot.textures.allSatisfy { $0.reused })
        XCTAssertEqual(cold.textures.map { $0.rgba }, hot.textures.map { $0.rgba })
        let known = cache.known
        XCTAssertTrue(try m.generate(known: known).textures.allSatisfy { $0.rgba.isEmpty })
        cache.removeAll()
        XCTAssertThrowsError(try m.generate(textures: cache, known: known))
        XCTAssertThrowsError(try m.generate(lod: 8))
        XCTAssertThrowsError(try m.generate(inputs: ["wide": .float(0)]))
        let token = try CancellationToken()
        token.cancel()
        XCTAssertThrowsError(try m.generate(limits: ExecutionLimits(cancellation: token)))
        DispatchQueue.concurrentPerform(iterations: 48) { i in
            do {
                let seed = UInt32(100 + i % 8)
                let result = try m.generate(seed: seed, textures: cache)
                let single = try m.generate(seed: seed)
                XCTAssertEqual(result.surfaces.map { $0.positions }, single.surfaces.map { $0.positions })
                XCTAssertEqual(result.textures.map { $0.rgba }, single.textures.map { $0.rgba })
            } catch { XCTFail(String(describing: error)) }
        }
        m.close()
        m.close()
        XCTAssertThrowsError(try m.generate())
        XCTAssertFalse(cold.textures[0].rgba.isEmpty)
    }
}
#endif
