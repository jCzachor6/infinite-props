import XCTest
import InfiniteProps
import Metal

/// Auxiliary images against the CLI-rendered shared fixtures.
final class NormalMapTests: XCTestCase {
    private var fixtures: URL {
        if let path = ProcessInfo.processInfo.environment["IPROP_FIXTURES"] {
            return URL(fileURLWithPath: path)
        }
        return URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .appendingPathComponent("../../../../build/binding-fixtures").standardized
    }
    private func bytes(_ name: String) throws -> Data { try Data(contentsOf: fixtures.appendingPathComponent(name)) }

    func testAuthoredNormalMapMatchesTheCLI() throws {
        let program = try Program(contentsOf: fixtures.appendingPathComponent("binding-normal.iprop"))
        XCTAssertEqual(program.images.map(\.index), [0])
        XCTAssertEqual(program.images.map(\.kind), [.normal])
        XCTAssertEqual(program.images.map(\.name), ["relief"])
        let result = try program.generate(seed: 5)
        XCTAssertEqual(result.rgba, try bytes("binding-normal.rgba"))
        XCTAssertEqual(result.images.count, 1)
        let normal = try XCTUnwrap(result.normal)
        XCTAssertEqual(normal, result.images[0])
        XCTAssertEqual(normal.index, 0)
        XCTAssertEqual(normal.kind, .normal)
        XCTAssertEqual(normal.source, .authored)
        XCTAssertEqual(normal.name, "relief")
        XCTAssertEqual([normal.width, normal.height, UInt32(normal.rowStride)], [8, 4, 32])
        XCTAssertEqual([normal.pixelFormat, normal.alphaMode, normal.storageOrigin], [result.pixelFormat, 2, result.storageOrigin])
        XCTAssertEqual(normal.rgba, try bytes("binding-normal.normal.rgba"))
        XCTAssertTrue(stride(from: 3, to: normal.rgba.count, by: 4).allSatisfy { normal.rgba[$0] == 255 })
        // The seed moves one painted direction; a size override scales the image.
        XCTAssertNotEqual(try program.generate(seed: 6).normal?.rgba, normal.rgba)
        let scaled = try XCTUnwrap(try program.generate(seed: 5, width: 16, height: 8).normal)
        XCTAssertEqual([scaled.width, scaled.height, UInt32(scaled.rgba.count)], [16, 8, 512])
        // An authored image wins over a derive request and stays identical.
        XCTAssertEqual(try program.generate(seed: 5, deriveNormal: DerivedNormal()).images, result.images)
        XCTAssertEqual(try program.generate(seed: 5, deriveNormal: DerivedNormal(depth: 9)).normal, normal)
        // Conversions own their bytes after the program is closed.
        let cg = try normal.makeCGImage()
        XCTAssertEqual(try normal.makeSKTexture().size(), CGSize(width: 8, height: 4))
        program.close()
        XCTAssertEqual([cg.width, cg.height], [8, 4])
        XCTAssertEqual(cg.alphaInfo, .noneSkipLast)
        XCTAssertEqual(cg.dataProvider!.data! as Data, normal.rgba)
        XCTAssertEqual(Array(normal.rgba.prefix(4)), [128, 128, 255, 255])
        if let device = MTLCreateSystemDefaultDevice() {
            let texture = try normal.makeMetalTexture(device: device)
            XCTAssertEqual([texture.width, texture.height], [8, 4])
            XCTAssertEqual(texture.pixelFormat, .rgba8Unorm)
            var copied = [UInt8](repeating: 0, count: normal.rgba.count)
            texture.getBytes(&copied, bytesPerRow: 32, from: MTLRegionMake2D(0, 0, 8, 4), mipmapLevel: 0)
            XCTAssertEqual(Data(copied), normal.rgba)
        }
    }

    func testDerivedNormalMapIsOptIn() throws {
        let program = try Program(contentsOf: fixtures.appendingPathComponent("bindings.iprop"))
        XCTAssertEqual(program.images, [])
        let plain = try program.generate(seed: 5)
        XCTAssertEqual(plain.images, [])
        XCTAssertNil(plain.normal)
        for (request, name) in [(DerivedNormal(), "bindings-derived"),
                                (DerivedNormal(depth: 2, rounding: 0.5, detail: 0.25), "bindings-derived-custom")] {
            let result = try program.generate(seed: 5, deriveNormal: request)
            let normal = try XCTUnwrap(result.normal)
            XCTAssertEqual(normal.kind, .normal)
            XCTAssertEqual(normal.source, .derived)
            XCTAssertEqual(normal.name, "normal")
            XCTAssertEqual(normal.rgba, try bytes(name + ".normal.rgba"))
            XCTAssertEqual([normal.width, normal.height, normal.alphaMode], [8, 4, 2])
            // The request leaves the main image, outputs and fuel alone and spends work.
            XCTAssertEqual(result.rgba, plain.rgba)
            XCTAssertEqual(result.outputs.map(\.values), plain.outputs.map(\.values))
            XCTAssertEqual(result.statistics.fuelUsed, plain.statistics.fuelUsed)
            XCTAssertGreaterThan(result.statistics.workUsed, plain.statistics.workUsed)
        }
        // The derivation runs under the generation's work budget.
        let used = try program.generate(seed: 5, deriveNormal: DerivedNormal()).statistics.workUsed
        XCTAssertEqual(try program.generate(seed: 5, limits: ExecutionLimits(workLimit: used),
                                            deriveNormal: DerivedNormal()).statistics.workRemaining, 0)
        XCTAssertThrowsError(try program.generate(seed: 5, limits: ExecutionLimits(workLimit: used - 1),
                                                  deriveNormal: DerivedNormal())) { error in
            XCTAssertEqual((error as? IpropError)?.reason, 3015)
            XCTAssertEqual((error as? IpropError)?.stage, 4)
        }
    }

    func testInvalidRequestsFailAsArguments() throws {
        let program = try Program(contentsOf: fixtures.appendingPathComponent("bindings.iprop"))
        for request in [DerivedNormal(depth: -1), DerivedNormal(depth: .infinity), DerivedNormal(rounding: 64.5),
                        DerivedNormal(rounding: -0.5), DerivedNormal(detail: 1.5), DerivedNormal(detail: .nan)] {
            XCTAssertThrowsError(try program.generate(seed: 5, deriveNormal: request)) { error in
                XCTAssertEqual((error as? IpropError)?.status, 4)
                XCTAssertEqual((error as? IpropError)?.stage, 3)
                XCTAssertTrue((error as? IpropError)?.message.contains("out of range") ?? false)
            }
        }
        XCTAssertNil(try program.generate(seed: 5).normal)
    }
}
