#if IPROP_MODELS
import CIprop
import Foundation

public struct ModelMetadata: Sendable {
    public let size: SIMD3<Float>
    public let unitsPerMeter: Float
    public let fuel: UInt64
    public let containerFlags: UInt32
}

public struct ModelLibrary: Sendable {
    public let apiMajor, apiMinor, maxVertices, maxTriangles, maxTextures, maxTextureSide: UInt32
    public let maxTexels, textureStamp: UInt64
    public let maxLod: UInt32 = 7
}

public struct SkinProgram: Sendable {
    public let index, width, height: UInt32
    public let name: String
}

public struct Surface: Sendable {
    public let index, textureIndex: UInt32
    public let roughness, metallic, emission, alphaCutoff: Float
    public let alphaMode: UInt32
    public let twoSided, crisp: Bool
    public let positions, normals, tangents, uvs: [Float]
    public let indices: [UInt32]
}

public struct KnownTexture: Sendable {
    public let key, fuel, work: UInt64
    public let imageKinds: UInt32
    public var keyHex: String { String(format: "%016llx", key) }
    public init(key: UInt64, fuel: UInt64, work: UInt64, imageKinds: UInt32) {
        self.key = key
        self.fuel = fuel
        self.work = work
        self.imageKinds = imageKinds
    }
}

public struct ModelTexture: Sendable {
    public let known: KnownTexture
    public var key: UInt64 { known.key }
    public let index, assetWidth, assetHeight, width, height: UInt32
    public let repeats, reused: Bool
    public let name: String
    public let rowStride: UInt64
    public let pixelFormat, alphaMode, origin: UInt32
    public let rgba: Data
    public let images: [AuxiliaryImage]
}

public struct ModelOutput: Sendable {
    public let index, kind, valueCount, floatsPerValue: UInt32
    public let name: String
    public let isList: Bool
    public let values: [Float]
}

public struct ModelStatistics: Sendable {
    public let vertexCount, triangleCount: UInt32
    public let fuelBudget, fuelUsed, workBudget, workUsed: UInt64
}

public struct GeneratedModel: Sendable {
    public let size, boundsMin, boundsMax: SIMD3<Float>
    public let unitsPerMeter: Float
    public let statistics: ModelStatistics
    public let surfaces: [Surface]
    public let textures: [ModelTexture]
    public let outputs: [ModelOutput]
}

public final class TextureCache: @unchecked Sendable {
    private let lock = NSLock()
    private var entries: [UInt64: ModelTexture] = [:]
    public init() {}
    fileprivate func snapshot() -> [UInt64: ModelTexture] { lock.withLock { entries } }
    public var known: [KnownTexture] { snapshot().values.map { $0.known } }
    public var count: Int { lock.withLock { entries.count } }
    public func texture(for key: UInt64) -> ModelTexture? { lock.withLock { entries[key] } }
    public func remove(_ key: UInt64) { lock.withLock { entries[key] = nil } }
    public func removeAll() { lock.withLock { entries.removeAll() } }
    fileprivate func store(_ textures: [ModelTexture]) {
        lock.withLock {
            for texture in textures where !texture.reused { entries[texture.key] = texture }
        }
    }
}

private final class ModelOwner {
    let handle: OpaquePointer
    init(_ handle: OpaquePointer) { self.handle = handle }
    deinit { iprop3d_model_free(handle) }
}

public enum ModelError: Error { case incompatibleVersion, closed, missingTexture(UInt64) }

public final class Model: @unchecked Sendable {
    private let lock = NSLock()
    private var owner: ModelOwner?
    public let metadata: ModelMetadata
    public let inputs: [Input]
    public let skinPrograms: [SkinProgram]

    public static func library() throws -> ModelLibrary {
        try requireRuntime()
        var info = iprop3d_library_info()
        info.struct_size = UInt32(MemoryLayout.size(ofValue: info))
        try checked { iprop3d_library_query(&info, $0) }
        guard info.api_major == 0, info.api_minor == 3 else { throw ModelError.incompatibleVersion }
        return ModelLibrary(apiMajor: info.api_major, apiMinor: info.api_minor, maxVertices: info.max_vertices,
                            maxTriangles: info.max_triangles, maxTextures: info.max_textures,
                            maxTextureSide: info.max_texture_side, maxTexels: info.max_texels,
                            textureStamp: iprop3d_texture_stamp())
    }

    public convenience init(contentsOf url: URL) throws { try self.init(bytes: Data(contentsOf: url)) }
    public init(bytes: Data) throws {
        _ = try Self.library()
        var handle: OpaquePointer?
        try (bytes.isEmpty ? Data([0]) : bytes).withUnsafeBytes { storage in
            try checked { iprop3d_model_load(storage.bindMemory(to: UInt8.self).baseAddress,
                                             UInt64(bytes.count), &handle, $0) }
        }
        owner = ModelOwner(handle!)
        var info = iprop3d_model_info()
        info.struct_size = UInt32(MemoryLayout.size(ofValue: info))
        try checked { iprop3d_model_query(handle, &info, $0) }
        metadata = ModelMetadata(size: SIMD3(info.size_x, info.size_y, info.size_z),
                                 unitsPerMeter: info.units_per_meter, fuel: info.fuel, containerFlags: info.container_flags)
        inputs = try (0..<info.input_count).map { index in
            var input = iprop_input_info_ex()
            input.struct_size = UInt32(MemoryLayout.size(ofValue: input))
            try checked { iprop3d_model_input_get(handle, index, &input, $0) }
            return declaredInput(input)
        }
        skinPrograms = try (0..<info.skin_program_count).map { index in
            var skin = iprop3d_skin_program_info()
            skin.struct_size = UInt32(MemoryLayout.size(ofValue: skin))
            try checked { iprop3d_model_skin_program(handle, index, &skin, $0) }
            return SkinProgram(index: index, width: skin.width, height: skin.height, name: string(skin.name))
        }
    }

    public func close() { lock.withLock { owner = nil } }

    public func generate(seed: UInt32 = 0, inputs: [String: InputValue] = [:], lod: UInt32 = 0,
                         textures: TextureCache? = nil, known: [KnownTexture] = [],
                         limits: ExecutionLimits = ExecutionLimits()) throws -> GeneratedModel {
        try generate(seed: seed, overrides: inputs.map { InputOverride($0.key, $0.value) }, lod: lod,
                     textures: textures, known: known, limits: limits)
    }

    public func generate(seed: UInt32 = 0, overrides: [InputOverride], lod: UInt32 = 0,
                         textures: TextureCache? = nil, known: [KnownTexture] = [],
                         limits: ExecutionLimits = ExecutionLimits()) throws -> GeneratedModel {
        guard let held = lock.withLock({ owner }) else { throw ModelError.closed }
        defer { withExtendedLifetime(held) {} }
        var parameters: OpaquePointer?
        try checked { iprop3d_model_parameters_create(held.handle, &parameters, $0) }
        defer { iprop_parameters_free(parameters) }
        try setOverrides(overrides, parameters: parameters)
        let snapshot = textures?.snapshot()
        let records = ((snapshot?.values.map { $0.known } ?? []) + known).map {
            iprop3d_known_texture(key: $0.key, fuel: $0.fuel, work: $0.work, image_kinds: $0.imageKinds, reserved: 0)
        }
        var options = iprop3d_generate_options()
        options.struct_size = UInt32(MemoryLayout.size(ofValue: options))
        options.seed = seed
        options.lod = lod
        var control = iprop_execution_limits_ex()
        control.struct_size = UInt32(MemoryLayout.size(ofValue: control))
        control.work_limit = limits.workLimit
        control.timeout_ns = limits.timeoutNanoseconds
        control.cancellation = limits.cancellation?.handle
        var result: OpaquePointer?
        try withExtendedLifetime(limits.cancellation) {
            try records.withUnsafeBufferPointer { buffer in
                options.known_textures = buffer.baseAddress
                options.known_texture_count = UInt32(buffer.count)
                try checked { iprop3d_generate(held.handle, &options, parameters, &control, &result, $0) }
            }
        }
        defer { iprop3d_result_free(result) }
        let model = try copyResult(result, snapshot)
        textures?.store(model.textures)
        return model
    }
}

private func copyArray<T>(_ pointer: UnsafePointer<T>?, _ count: Int) -> [T] {
    Array(UnsafeBufferPointer(start: pointer, count: count))
}

private func copyResult(_ result: OpaquePointer?, _ cache: [UInt64: ModelTexture]?) throws -> GeneratedModel {
    var r = iprop3d_result_info()
    r.struct_size = UInt32(MemoryLayout.size(ofValue: r))
    try checked { iprop3d_result_query(result, &r, $0) }
    let surfaces = try (0..<r.surface_count).map { i in
        var s = iprop3d_surface_view()
        s.struct_size = UInt32(MemoryLayout.size(ofValue: s))
        try checked { iprop3d_result_surface(result, i, &s, $0) }
        let count = Int(s.vertex_count)
        return Surface(index: i, textureIndex: s.texture_index, roughness: s.roughness, metallic: s.metallic,
                       emission: s.emission, alphaCutoff: s.alpha_cutoff, alphaMode: s.alpha_mode,
                       twoSided: s.two_sided != 0, crisp: s.crisp != 0, positions: copyArray(s.positions, count*3),
                       normals: copyArray(s.normals, count*3), tangents: copyArray(s.tangents, count*4),
                       uvs: copyArray(s.uvs, count*2), indices: copyArray(s.indices, Int(s.index_count)))
    }
    let outputs = try (0..<r.output_count).map { i in
        var o = iprop3d_output_view()
        o.struct_size = UInt32(MemoryLayout.size(ofValue: o))
        try checked { iprop3d_result_output(result, i, &o, $0) }
        return ModelOutput(index: i, kind: o.kind, valueCount: o.value_count, floatsPerValue: o.floats_per_value,
                           name: string(o.name), isList: o.list != 0, values: copyArray(o.values, Int(o.value_count)*Int(o.floats_per_value)))
    }
    let textures = try (0..<r.texture_count).map { i in
        var t = iprop3d_texture_view()
        t.struct_size = UInt32(MemoryLayout.size(ofValue: t))
        try checked { iprop3d_result_texture(result, i, &t, $0) }
        let stored = t.reused == 0 ? nil : cache?[t.key]
        if t.reused != 0 && cache != nil && stored == nil { throw ModelError.missingTexture(t.key) }
        let images = try stored?.images ?? (0..<t.image_count).compactMap { j -> AuxiliaryImage? in
            var a = iprop_aux_image_view_ex()
            a.struct_size = UInt32(MemoryLayout.size(ofValue: a))
            try checked { iprop3d_result_texture_image(result, i, j, &a, $0) }
            guard let kind = ImageKind(rawValue: a.kind), let source = ImageSource(rawValue: a.source) else { return nil }
            return AuxiliaryImage(index: j, kind: kind, source: source, name: string(a.name), width: a.width,
                                  height: a.height, rowStride: a.row_stride, pixelFormat: a.pixel_format,
                                  alphaMode: a.alpha_mode, storageOrigin: a.origin,
                                  rgba: Data(bytes: a.pixels!, count: Int(a.byte_count)))
        }
        return ModelTexture(known: KnownTexture(key: t.key, fuel: t.fuel, work: t.work, imageKinds: t.image_kinds),
                            index: i, assetWidth: t.asset_width, assetHeight: t.asset_height, width: t.width,
                            height: t.height, repeats: t.repeats != 0, reused: t.reused != 0, name: string(t.name),
                            rowStride: stored?.rowStride ?? t.row_stride, pixelFormat: stored?.pixelFormat ?? t.pixel_format,
                            alphaMode: stored?.alphaMode ?? t.alpha_mode, origin: stored?.origin ?? t.origin,
                            rgba: stored?.rgba ?? (t.byte_count == 0 ? Data() : Data(bytes: t.pixels!, count: Int(t.byte_count))),
                            images: images)
    }
    func triple(_ t: (Float, Float, Float)) -> SIMD3<Float> { SIMD3(t.0, t.1, t.2) }
    return GeneratedModel(size: triple(r.size), boundsMin: triple(r.bounds_min), boundsMax: triple(r.bounds_max),
                          unitsPerMeter: r.units_per_meter, statistics: ModelStatistics(vertexCount: r.vertex_count,
                          triangleCount: r.triangle_count, fuelBudget: r.fuel_budget, fuelUsed: r.fuel_used,
                          workBudget: r.work_budget, workUsed: r.work_used), surfaces: surfaces, textures: textures, outputs: outputs)
}
#endif
