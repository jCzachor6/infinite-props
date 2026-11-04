import Foundation
import CIprop

public struct Point: Sendable, Equatable {
    public let x, y: Float
    public init(_ x: Float, _ y: Float) { self.x = x; self.y = y }
}

/// Complete C API snapshot; resource IDs are local to its GeneratedImage.
/// D4 and translation already include static imports and pixel rounding.
public struct PartSnapshot: Sendable {
    public let outputIndex, elementIndex: UInt32
    public let id, sourceId: Int32
    public let shapeKind, appearanceKind, shapeResource, regionIndex, textureResource: UInt32
    public let tint: RGBA8
    public let d4: UInt32
    public let translationX, translationY: Int32
    public let sourceScale, targetScale, sourceAnchor, center, boundsOrigin, boundsSize: Point
    public let sourceD4: UInt32
    public let sourceOriginX, sourceOriginY: Int32
    public let textureD4: UInt32
    public let textureOriginX, textureOriginY: Int32
}

public struct ResourceRing: Sendable {
    public let index, parentRing: UInt32
    public let isHole: Bool
    public let vertices: [Point]
}
public struct ResourceRegion: Sendable {
    public let index, outerRing: UInt32
    public let holes: [UInt32]
    public let sourceId: Int32
    public let area: Float
    public let center, boundsOrigin, boundsSize: Point
}

/// Exactly one owned copy per result-local ID. Raster data keeps exact A8 or
/// RGBA bytes; geometry retains ordered rings and explicit hole assignments.
public struct ResultResource: Sendable {
    public let id, kind, pixelFormat, width, height: UInt32
    public let rowStride: UInt64
    public let data: Data
    public let scale: Point
    public let vertexCount, edgeCount: UInt32
    public let rings: [ResourceRing]
    public let regions: [ResourceRegion]
    public var byteCount: UInt64 { UInt64(data.count) }
}

private func point(_ p: iprop_point_value) -> Point { Point(p.x, p.y) }

func readPartSnapshots(_ result: OpaquePointer?, _ slot: UInt32, _ count: UInt32) throws -> [PartSnapshot] {
    try (0..<count).map { index in
        var p = iprop_part_snapshot_ex()
        iprop_part_snapshot_ex_init(&p, UInt32(MemoryLayout<iprop_part_snapshot_ex>.size))
        try checked { iprop_result_output_part(result, slot, index, &p, $0) }
        return PartSnapshot(outputIndex: p.output_index, elementIndex: p.element_index, id: p.id, sourceId: p.source_id,
            shapeKind: p.shape_kind, appearanceKind: p.appearance_kind, shapeResource: p.shape_resource,
            regionIndex: p.region_index, textureResource: p.texture_resource,
            tint: RGBA8(p.tint.r, p.tint.g, p.tint.b, p.tint.a), d4: p.d4,
            translationX: p.translation_x, translationY: p.translation_y,
            sourceScale: point(p.source_scale), targetScale: point(p.target_scale), sourceAnchor: point(p.source_anchor),
            center: point(p.center), boundsOrigin: point(p.bounds_origin), boundsSize: point(p.bounds_size),
            sourceD4: p.source_d4, sourceOriginX: p.source_origin_x, sourceOriginY: p.source_origin_y,
            textureD4: p.texture_d4, textureOriginX: p.texture_origin_x, textureOriginY: p.texture_origin_y)
    }
}

func readPartResources(_ result: OpaquePointer?) throws -> [ResultResource] {
    var count: UInt32 = 0
    try checked { iprop_result_resource_count(result, &count, $0) }
    var resources: [ResultResource] = []
    for index in 0..<count {
        let id = index + 1
        var info = iprop_resource_info_ex()
        iprop_resource_info_ex_init(&info, UInt32(MemoryLayout<iprop_resource_info_ex>.size))
        try checked { iprop_result_resource_get(result, id, &info, $0) }
        let data = info.byte_count == 0 ? Data() : Data(bytes: info.data!, count: Int(info.byte_count))
        var rings: [ResourceRing] = []
        for r in 0..<info.ring_count {
            var ring = iprop_ring_info_ex()
            iprop_ring_info_ex_init(&ring, UInt32(MemoryLayout<iprop_ring_info_ex>.size))
            try checked { iprop_result_resource_ring(result, id, r, &ring, $0) }
            var vertices = [iprop_point_value](repeating: iprop_point_value(), count: Int(ring.vertex_count))
            if !vertices.isEmpty {
                try vertices.withUnsafeMutableBytes { buffer in
                    try checked { iprop_result_resource_ring_copy_vertices(result, id, r,
                        buffer.baseAddress!.assumingMemoryBound(to: iprop_point_value.self), UInt64(buffer.count), $0) }
                }
            }
            rings.append(ResourceRing(index: ring.ring_index, parentRing: ring.parent_ring,
                isHole: ring.is_hole != 0, vertices: vertices.map(point)))
        }
        var regions: [ResourceRegion] = []
        for r in 0..<info.region_count {
            var region = iprop_region_info_ex()
            iprop_region_info_ex_init(&region, UInt32(MemoryLayout<iprop_region_info_ex>.size))
            try checked { iprop_result_resource_region(result, id, r, &region, $0) }
            var holes: [UInt32] = []
            for h in 0..<region.hole_count {
                var hole: UInt32 = 0
                try checked { iprop_result_resource_region_hole(result, id, r, h, &hole, $0) }
                holes.append(hole)
            }
            regions.append(ResourceRegion(index: region.region_index, outerRing: region.outer_ring, holes: holes,
                sourceId: region.source_id, area: region.area, center: point(region.center),
                boundsOrigin: point(region.bounds_origin), boundsSize: point(region.bounds_size)))
        }
        resources.append(ResultResource(id: info.resource_id, kind: info.kind, pixelFormat: info.pixel_format,
            width: info.width, height: info.height, rowStride: info.row_stride, data: data, scale: point(info.scale),
            vertexCount: info.vertex_count, edgeCount: info.edge_count, rings: rings, regions: regions))
    }
    return resources
}
