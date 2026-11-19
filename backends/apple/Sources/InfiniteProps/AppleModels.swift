#if IPROP_MODELS
import Foundation
import SceneKit

extension GeneratedModel {
    /// One geometry element and physically based material per model surface.
    public func makeSCNGeometry() throws -> SCNGeometry {
        var positions = [Float](), normals = [Float](), tangents = [Float](), uvs = [Float]()
        var elements = [SCNGeometryElement](), materials = [SCNMaterial]()
        for surface in surfaces {
            let base = UInt32(positions.count / 3)
            positions.append(contentsOf: surface.positions.map { $0 / unitsPerMeter })
            normals.append(contentsOf: surface.normals)
            for (i, value) in surface.tangents.enumerated() {
                tangents.append(i % 4 == 3 ? -value : value)
            }
            uvs.append(contentsOf: surface.uvs)
            let indices = surface.indices.map { base + $0 }
            elements.append(SCNGeometryElement(indices: indices, primitiveType: .triangles))
            materials.append(try textures[Int(surface.textureIndex)].makeSCNMaterial(for: surface))
        }
        func source(_ values: [Float], _ semantic: SCNGeometrySource.Semantic, _ components: Int) -> SCNGeometrySource {
            SCNGeometrySource(data: values.withUnsafeBytes { Data($0) }, semantic: semantic,
                vectorCount: values.count / components, usesFloatComponents: true,
                componentsPerVector: components, bytesPerComponent: 4, dataOffset: 0, dataStride: 4 * components)
        }
        let geometry = SCNGeometry(sources: [source(positions, .vertex, 3), source(normals, .normal, 3),
            source(tangents, .tangent, 4), source(uvs, .texcoord, 2)], elements: elements)
        geometry.materials = materials
        return geometry
    }
}

extension ModelTexture {
    public func makeSCNMaterial(for surface: Surface) throws -> SCNMaterial {
        let material = SCNMaterial()
        material.lightingModel = .physicallyBased
        material.isDoubleSided = surface.twoSided
        let colour = try makeCGImage()
        material.diffuse.contents = colour
        material.roughness.contents = NSNumber(value: surface.roughness)
        material.metalness.contents = NSNumber(value: surface.metallic)
        if surface.emission > 0 {
            material.emission.contents = colour
            material.emission.intensity = CGFloat(surface.emission)
        }
        for image in images {
            let property: SCNMaterialProperty
            switch image.kind {
            case .normal: property = material.normal
            case .ao: property = material.ambientOcclusion
            case .roughness: property = material.roughness; property.intensity = CGFloat(surface.roughness)
            case .metallic: property = material.metalness; property.intensity = CGFloat(surface.metallic)
            case .emission: property = material.emission; property.intensity = CGFloat(surface.emission)
            }
            property.contents = try image.makeCGImage()
        }
        for p in [material.diffuse, material.normal, material.roughness, material.metalness,
                  material.emission, material.ambientOcclusion] {
            p.wrapS = repeats ? .repeat : .clamp
            p.wrapT = p.wrapS
            p.magnificationFilter = surface.crisp ? .nearest : .linear
            p.minificationFilter = p.magnificationFilter
            p.mipFilter = surface.crisp ? .none : .linear
        }
        if surface.alphaMode == 2 {
            material.blendMode = .alpha
            material.writesToDepthBuffer = false
        } else {
            let cutout = surface.alphaMode == 1 ? "if (_surface.diffuse.a < \(surface.alphaCutoff)) discard_fragment();" : ""
            material.shaderModifiers = [.surface: "#pragma body\n\(cutout)\n_surface.diffuse.a = 1.0;"]
        }
        return material
    }
}
#endif
