import XCTest
import InfiniteProps
import Metal

final class RuntimeTests: XCTestCase {
    private var fixtures: URL {
        if let path = ProcessInfo.processInfo.environment["IPROP_FIXTURES"] {
            return URL(fileURLWithPath: path)
        }
        return URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .appendingPathComponent("../../../../build/binding-fixtures").standardized
    }

    func testGenerationInputsOwnershipAndErrors() throws {
        let program = try Program(contentsOf: fixtures.appendingPathComponent("bindings.iprop"))
        XCTAssertEqual(program.metadata.width, 8)
        XCTAssertEqual(program.metadata.height, 4)
        XCTAssertEqual(program.metadata.originX, 2)
        XCTAssertEqual(program.inputs.map(\.name), ["tint", "amount", "offset"])
        let result = try program.generate(seed: UInt32.max, inputs: [
            "tint": .color(RGBA8(18, 52, 86)), "amount": .float(-0.0), "offset": .int(-7)])
        XCTAssertEqual(result.rgba, try Data(contentsOf: fixtures.appendingPathComponent("bindings.rgba")))
        XCTAssertEqual(result.output(named: "anchor")!.values.map(\.bitPattern), [0x80000000, 0xc0e00000])
        XCTAssertEqual(result.output(named: "marks")!.count, 2)
        XCTAssertEqual(result.output(named: "unset")!.count, 0)
        XCTAssertEqual(result.statistics.fuelBudget - result.statistics.fuelRemaining, result.statistics.fuelUsed)
        XCTAssertEqual(result.statistics.imageByteCount, 128)
        let defaults = try program.generate()
        XCTAssertNotEqual(defaults.rgba, result.rgba)
        XCTAssertEqual(try program.generate(seed: UInt32.max, width: 16, height: 8).rgba.count, 512)
        for (name, value, reason) in [("missing", InputValue.uint(0), UInt32(4005)),
                                     ("tint", InputValue.float(1), UInt32(4004))] {
            XCTAssertThrowsError(try program.generate(inputs: [name: value])) { error in
                XCTAssertEqual((error as? IpropError)?.reason, reason)
                XCTAssertEqual((error as? IpropError)?.stage, 3)
            }
        }
        // Conversion must own its backing bytes after both result and program die.
        let cg = try result.makeCGImage()
        XCTAssertEqual(cg.width, 8)
        XCTAssertEqual(try result.makeSKTexture().size(), CGSize(width: 8, height: 4))
        program.close(); program.close()
        XCTAssertEqual(result.rgba.count, 128)
        XCTAssertEqual(cg.dataProvider!.data! as Data, result.rgba)
        XCTAssertThrowsError(try program.generate())
        XCTAssertThrowsError(try Program(bytes: Data())) { error in
            XCTAssertEqual((error as? IpropError)?.status, 2)
        }
    }

    func testExactNamesAndFloatBits() throws {
        let program = try Program(contentsOf: fixtures.appendingPathComponent("backend-raw-values.iprop"))
        let result = try program.generate()
        XCTAssertEqual(result.outputs.map(\.owner), [0, 0, 0, 2, 2, 2])
        XCTAssertEqual(result.outputs.prefix(3).map(\.name), ["pł\0suffix", "", "\0c\0\0"])
        XCTAssertEqual(result.outputs.prefix(3).flatMap(\.values).map(\.bitPattern),
            [0x80000000, 1, 0x7fc12345, 0xff800000, 0x7f800000, 0xffc12345, 0x80000001, 0x3f800000, 0x80000000])
        XCTAssertEqual(result.outputs.suffix(3).map(\.count), [0, 0, 0])
    }

    func testConcurrentProgramCalls() throws {
        let program = try Program(contentsOf: fixtures.appendingPathComponent("bindings.iprop"))
        let expected = try program.generate(seed: 17).rgba
        DispatchQueue.concurrentPerform(iterations: 20) { _ in
            do {
                let actual = try program.generate(seed: 17)
                XCTAssertEqual(actual.rgba, expected)
            } catch { XCTFail("\(error)") }
        }
    }

    func testMetalUploadAndReadback() throws {
        guard let device = MTLCreateSystemDefaultDevice() else { throw XCTSkip("Metal device unavailable on this host") }
        let program = try Program(contentsOf: fixtures.appendingPathComponent("bindings.iprop"))
        let image = try program.generate(seed: 7)
        let texture = try image.makeMetalTexture(device: device)
        var readback = [UInt8](repeating: 0, count: 128)
        texture.getBytes(&readback, bytesPerRow: 32, from: MTLRegionMake2D(0, 0, 8, 4), mipmapLevel: 0)
        XCTAssertEqual(Data(readback), image.rgba)
    }

    func testUnicodeInputsAndLocatedRuntimeFailure() throws {
        let names = try Program(contentsOf: fixtures.appendingPathComponent("binding-unicode.iprop"))
        XCTAssertEqual(names.inputs.first!.name, "pł\0suffix")
        let image = try names.generate(inputs: ["pł\0suffix": .color(RGBA8(18, 52, 86))])
        XCTAssertEqual(image.rgba, Data([18, 52, 86, 255]))
        let distinct = try names.generate(overrides: [InputOverride("é", .float(1)), InputOverride("e\u{301}", .float(2))])
        XCTAssertEqual(distinct.output(named: "anchor")!.values, [1, 2])
        XCTAssertThrowsError(try names.generate(overrides: [InputOverride("é", .float(1)), InputOverride("é", .float(2))])) { error in
            XCTAssertEqual((error as? IpropError)?.reason, 4006)
        }
        let outputNames = try Program(contentsOf: fixtures.appendingPathComponent("backend-normalized-names.iprop"))
        let outputs = try outputNames.generate()
        XCTAssertEqual(outputs.output(named: "é")!.kind, .rect)
        XCTAssertEqual(outputs.output(named: "e\u{301}")!.kind, .circle)
        XCTAssertThrowsError(try names.generate(inputs: ["pł\0suffix\0extra": .color(RGBA8(0, 0, 0))])) { error in
            XCTAssertEqual((error as? IpropError)?.reason, 4005)
        }
        let trap = try Program(contentsOf: fixtures.appendingPathComponent("binding-trap.iprop"))
        XCTAssertThrowsError(try trap.generate()) { error in
            let failure = error as! IpropError
            XCTAssertEqual(failure.status, 3)
            XCTAssertEqual(failure.reason, 3008)
            XCTAssertEqual(failure.stage, 4)
            XCTAssertNotEqual(failure.locationFlags & 8, 0)
            XCTAssertGreaterThan(failure.debugLine, 0)
        }
        XCTAssertEqual(try trap.generate(inputs: ["divisor": .int(1)]).output(named: "value")!.values, [1, 0])
    }
}
