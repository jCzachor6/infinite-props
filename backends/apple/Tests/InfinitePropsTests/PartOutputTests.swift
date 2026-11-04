import XCTest
import InfiniteProps

final class PartOutputTests: XCTestCase {
    private func point(_ p: InfiniteProps.Point) -> [UInt32] { [p.x.bitPattern, p.y.bitPattern] }

    private func observe(_ result: GeneratedImage) -> [String: Any] {
        let outputs: [[String: Any]] = result.outputs.enumerated().map { index, slot in
            XCTAssertEqual(slot.index, UInt32(index))
            let stride = [2, 4, 3, 0][Int(slot.kind.rawValue)]
            XCTAssertEqual(Int(slot.count), stride == 0 ? slot.parts.count : slot.values.count / stride)
            let parts: [[String: Any]] = slot.parts.map { p in
                XCTAssertNotNil(result.resource(p.shapeResource))
                if p.textureResource != 0 { XCTAssertNotNil(result.resource(p.textureResource)) }
                return ["slot": p.outputIndex, "entry": p.elementIndex, "id": p.id, "source_id": p.sourceId,
                    "shape": [p.shapeKind, p.shapeResource, p.regionIndex],
                    "appearance": [p.appearanceKind, p.textureResource, UInt32(p.tint.r), UInt32(p.tint.g), UInt32(p.tint.b), UInt32(p.tint.a)],
                    "source_mapping": [Int64(p.sourceD4), Int64(p.sourceOriginX), Int64(p.sourceOriginY)],
                    "texture_mapping": [Int64(p.textureD4), Int64(p.textureOriginX), Int64(p.textureOriginY)],
                    "transform": [Int64(p.d4), Int64(p.translationX), Int64(p.translationY)],
                    "source_scale": point(p.sourceScale), "target_scale": point(p.targetScale), "anchor": point(p.sourceAnchor),
                    "center": point(p.center), "bounds_origin": point(p.boundsOrigin), "bounds_size": point(p.boundsSize)]
            }
            let values = stride == 0 ? [] : Swift.stride(from: 0, to: slot.values.count, by: stride).map {
                Array(slot.values[$0..<$0 + stride]).map(\.bitPattern)
            }
            return ["name": slot.name, "owner": slot.owner, "kind": slot.kind.rawValue,
                "list": slot.isList, "values": values, "parts": parts]
        }
        let resources: [[String: Any]] = result.resources.enumerated().map { index, r in
            XCTAssertEqual(r.id, UInt32(index + 1)); XCTAssertEqual(r.byteCount, UInt64(r.data.count))
            if r.kind == 2 {
                return ["kind": "raster", "format": r.pixelFormat, "size": [r.width, r.height],
                    "scale": point(r.scale), "stride": r.rowStride, "bytes": Array(r.data)]
            }
            XCTAssertEqual(r.kind, 1)
            return ["kind": "geometry", "vertex_count": r.vertexCount, "edge_count": r.edgeCount,
                "rings": r.rings.enumerated().map { i, v -> [String: Any] in
                    XCTAssertEqual(v.index, UInt32(i))
                    return ["parent": v.parentRing, "hole": v.isHole, "vertices": v.vertices.map(point)]
                },
                "regions": r.regions.enumerated().map { i, v -> [String: Any] in
                    XCTAssertEqual(v.index, UInt32(i))
                    return ["outer": v.outerRing, "holes": v.holes, "source_id": v.sourceId,
                        "area": v.area.bitPattern, "center": point(v.center),
                        "bounds_origin": point(v.boundsOrigin), "bounds_size": point(v.boundsSize)]
                }]
        }
        return ["schema": 1, "size": [result.width, result.height], "rgba": Array(result.rgba),
            "outputs": outputs, "resources": resources]
    }

    func testCompleteCppCapiParityAndIndependentLifetime() throws {
        let base = URL(fileURLWithPath: ProcessInfo.processInfo.environment["IPROP_FIXTURES"]!)
        let cases = try JSONSerialization.jsonObject(with: Data(contentsOf: base.appendingPathComponent("part-bindings.json"))) as! [[String: Any]]
        XCTAssertEqual(cases.count, 23)
        for item in cases {
            let dir = base.appendingPathComponent(item["directory"] as! String)
            let expected = try JSONSerialization.jsonObject(with: Data(contentsOf: dir.appendingPathComponent("expected.json"))) as! NSDictionary
            let dimensions = item["size"] as! [UInt32], seed = (item["seed"] as! NSNumber).uint32Value
            let program = try Program(contentsOf: dir.appendingPathComponent("root.iprop"))
            let result = try program.generate(seed: seed, width: dimensions[0], height: dimensions[1])
            XCTAssertEqual(observe(result) as NSDictionary, expected, item["name"] as! String)
            _ = try program.generate(seed: seed ^ UInt32.max, width: dimensions[0], height: dimensions[1])
            program.close()
            XCTAssertEqual(observe(result) as NSDictionary, expected, "retained " + (item["name"] as! String))
            XCTAssertEqual(result.statistics.fuelUsed, result.statistics.fuelBudget - result.statistics.fuelRemaining)
            XCTAssertEqual(result.statistics.outputElementCount, result.outputs.reduce(0) { $0 + UInt64($1.count) })
            XCTAssertNil(result.resource(0)); XCTAssertNil(result.resource(UInt32(result.resources.count + 1)))
        }
    }
}
