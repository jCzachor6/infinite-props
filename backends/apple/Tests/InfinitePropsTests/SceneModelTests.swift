#if IPROP_MODELS && os(macOS)
import XCTest
import SceneKit
import Metal
@testable import InfiniteProps

final class SceneModelTests: XCTestCase {
    func testGeometryAndMaterials() throws {
        let fixtures = URL(fileURLWithPath: ProcessInfo.processInfo.environment["IPROP_FIXTURES"]!)
        let model = try Model(contentsOf: fixtures.appendingPathComponent("model-materials.iprop3d"))
        let result = try model.generate(seed: 1)
        let g = try result.makeSCNGeometry()
        XCTAssertEqual(g.elements.count, result.surfaces.count)
        XCTAssertEqual(g.materials.count, result.surfaces.count)
        let positions = result.surfaces.flatMap { $0.positions.map { $0 / result.unitsPerMeter } }
        XCTAssertEqual(g.sources(for: .vertex).first?.data, positions.withUnsafeBytes { Data($0) })
        for (i, s) in result.surfaces.enumerated() {
            XCTAssertEqual(g.elements[i].primitiveCount, s.indices.count / 3)
            XCTAssertEqual(g.materials[i].isDoubleSided, s.twoSided)
            XCTAssertEqual(g.materials[i].diffuse.magnificationFilter, s.crisp ? .nearest : .linear)
        }
    }

    /// A raised square on a front-facing plate. Light from above should reach
    /// its upper bevel and leave the downward-facing lower bevel in shadow.
    func testLitPlate() throws {
        guard let device = MTLCreateSystemDefaultDevice() else { throw XCTSkip("Metal device unavailable") }
        let side = 32
        var normal = Data()
        for y in 0..<side {
            for x in 0..<side {
                var green: UInt8 = 128
                if (6..<26).contains(x) {
                    if (6..<11).contains(y) { green = 241 }
                    if (21..<26).contains(y) { green = 15 }
                }
                normal.append(contentsOf: [128, green, green == 128 ? 255 : 184, 255])
            }
        }
        let map = AuxiliaryImage(index: 0, kind: .normal, source: .authored, name: "square", width: 32,
            height: 32, rowStride: 128, pixelFormat: 1, alphaMode: 2, storageOrigin: 0, rgba: normal)
        let t = ModelTexture(known: KnownTexture(key: 1, fuel: 0, work: 0, imageKinds: 2), index: 0,
            assetWidth: 32, assetHeight: 32, width: 32, height: 32, repeats: false, reused: false,
            name: "plate", rowStride: 128, pixelFormat: 1, alphaMode: 2, origin: 0,
            rgba: Data(repeating: 255, count: side*side*4), images: [map])
        let s = Surface(index: 0, textureIndex: 0, roughness: 1, metallic: 0, emission: 0, alphaCutoff: 0.5,
            alphaMode: 0, twoSided: false, crisp: true, positions: [-1,1,0, -1,-1,0, 1,-1,0, 1,1,0],
            normals: [Float](repeating: [0,0,1], count: 4), tangents: [Float](repeating: [1,0,0,1], count: 4),
            uvs: [0,0, 0,1, 1,1, 1,0], indices: [0,1,2, 0,2,3])
        let m = GeneratedModel(size: SIMD3(2,2,0), boundsMin: SIMD3(-1,-1,0), boundsMax: SIMD3(1,1,0),
            unitsPerMeter: 1, statistics: ModelStatistics(vertexCount: 4, triangleCount: 2, fuelBudget: 0,
                fuelUsed: 0, workBudget: 0, workUsed: 0), surfaces: [s], textures: [t], outputs: [])
        let scene = SCNScene()
        scene.rootNode.addChildNode(SCNNode(geometry: try m.makeSCNGeometry()))
        let camera = SCNNode()
        camera.camera = SCNCamera()
        camera.camera!.usesOrthographicProjection = true
        camera.camera!.orthographicScale = 1
        camera.position = SCNVector3(0,0,3)
        scene.rootNode.addChildNode(camera)
        let light = SCNNode()
        light.light = SCNLight()
        light.light!.type = .directional
        light.light!.intensity = 1200
        light.position = SCNVector3(0,2,1)
        light.look(at: SCNVector3Zero)
        scene.rootNode.addChildNode(light)
        let renderer = SCNRenderer(device: device, options: nil)
        renderer.scene = scene
        renderer.pointOfView = camera
        let image = renderer.snapshot(atTime: 0, with: CGSize(width: 128, height: 128), antialiasingMode: .none)
        let bitmap = try XCTUnwrap(NSBitmapImageRep(data: XCTUnwrap(image.tiffRepresentation)))
        // NSBitmapImageRep indexes the encoded scanlines from the top.
        func intensity(_ row: Int) throws -> CGFloat {
            let color = try XCTUnwrap(bitmap.colorAt(x: 64, y: row)?.usingColorSpace(.deviceRGB))
            return color.redComponent
        }
        XCTAssertGreaterThan(try intensity(33), try intensity(93) + 0.1)
    }
}

private extension Array where Element == Float {
    init(repeating vector: [Float], count: Int) { self = Array<[Float]>(repeating: vector, count: count).flatMap { $0 } }
}
#endif
