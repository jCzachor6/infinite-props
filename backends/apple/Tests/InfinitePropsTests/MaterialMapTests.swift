import XCTest
import InfiniteProps

/// Material maps (spec material maps v1.2) against the CLI-rendered shared fixture.
final class MaterialMapTests: XCTestCase {
    private var fixtures: URL {
        if let path = ProcessInfo.processInfo.environment["IPROP_FIXTURES"] {
            return URL(fileURLWithPath: path)
        }
        return URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .appendingPathComponent("../../../../build/binding-fixtures").standardized
    }
    private func bytes(_ name: String) throws -> Data { try Data(contentsOf: fixtures.appendingPathComponent(name)) }
    private let kinds: [ImageKind] = [.normal, .roughness, .metallic, .ao, .emission]
    private let names = ["relief", "gloss", "metal", "shade", "glow"]
    private let files = ["normal", "roughness", "metallic", "ao", "emission"]

    func testEveryMapMatchesTheCLIInKindOrder() throws {
        XCTAssertEqual(kinds.map(\.rawValue), [1, 2, 3, 4, 5])
        let program = try Program(contentsOf: fixtures.appendingPathComponent("binding-maps.iprop"))
        // Declared out of order in the source; listed by kind.
        XCTAssertEqual(program.images.map(\.index), [0, 1, 2, 3, 4])
        XCTAssertEqual(program.images.map(\.kind), kinds)
        XCTAssertEqual(program.images.map(\.name), names)
        let result = try program.generate(seed: 5)
        XCTAssertEqual(result.rgba, try bytes("binding-maps.rgba"))
        XCTAssertEqual(result.images.map(\.kind), kinds)
        XCTAssertEqual(result.images.map(\.name), names)
        for (index, image) in result.images.enumerated() {
            XCTAssertEqual(image.index, UInt32(index))
            XCTAssertEqual(image.source, .authored)
            XCTAssertEqual(result.image(image.kind), image)
            XCTAssertEqual([image.width, image.height, UInt32(image.rowStride), image.alphaMode], [8, 4, 32, 2])
            XCTAssertEqual(image.rgba, try bytes("binding-maps." + files[index] + ".rgba"))
            XCTAssertTrue(stride(from: 3, to: image.rgba.count, by: 4).allSatisfy { image.rgba[$0] == 255 })
        }
        // A scalar map holds one value in red, green and blue.
        for kind in [ImageKind.roughness, .metallic, .ao] {
            let pixels = try XCTUnwrap(result.image(kind)).rgba
            XCTAssertTrue(stride(from: 0, to: pixels.count, by: 4).allSatisfy { pixels[$0] == pixels[$0 + 1] && pixels[$0] == pixels[$0 + 2] })
        }
        XCTAssertEqual(result.normal, result.images[0])
        // The seed moves one roughness pixel and nothing else.
        let moved = try program.generate(seed: 6)
        XCTAssertNotEqual(moved.image(.roughness)?.rgba, result.image(.roughness)?.rgba)
        XCTAssertEqual(moved.image(.emission)?.rgba, result.image(.emission)?.rgba)
        // An authored normal map wins over a derive request; the maps stay.
        XCTAssertEqual(try program.generate(seed: 5, deriveNormal: DerivedNormal()).images, result.images)
    }

    func testAProgramWithoutMapsPublishesNone() throws {
        let program = try Program(contentsOf: fixtures.appendingPathComponent("bindings.iprop"))
        XCTAssertNil(try program.generate(seed: 5).image(.roughness))
    }
}
