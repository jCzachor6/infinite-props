import Foundation
import CIprop

public struct RGBA8: Sendable, Equatable {
    public let r, g, b, a: UInt8
    public init(_ r: UInt8, _ g: UInt8, _ b: UInt8, _ a: UInt8 = 255) {
        self.r = r; self.g = g; self.b = b; self.a = a
    }
}

/// Integers retain their 32-bit representation. The binary schema reports
/// both source i32 and u32 declarations as uint32; it cannot infer signedness.
public enum InputValue: Sendable, Equatable {
    case color(RGBA8), float(Float), uint(UInt32), int(Int32)
}

/// The array API can address distinct UTF-8 names that Swift Dictionary
/// considers canonically equivalent Unicode strings.
public struct InputOverride: Sendable {
    public let name: String
    public let value: InputValue
    public init(_ name: String, _ value: InputValue) { self.name = name; self.value = value }
}

public struct Input: Sendable {
    public let index: UInt32
    public let name: String
    public let defaultValue: InputValue
}

public struct Metadata: Sendable {
    public let width, height: UInt32
    public let originX, originY: Float
    public let fuel: UInt64
    public let emissionColor: RGBA8?
    public let emissionOffsetX, emissionOffsetY, emissionRadius: Float
    public let containerFlags: UInt32
}

public struct Statistics: Sendable {
    public let fuelBudget, fuelRemaining, fuelUsed, imageByteCount, outputElementCount: UInt64
}

public enum OutputKind: UInt32, Sendable { case point = 0, rect = 1, circle = 2 }

/// Packed x/y, x/y/w/h or x/y/radius values, in declaration order. An unset
/// singular or empty list remains present. Names include any embedded NUL.
public struct Output: Sendable {
    public let index, owner: UInt32
    public let name: String
    public let kind: OutputKind
    public let isList: Bool
    public let count: UInt32
    public let values: [Float]
}

public struct GeneratedImage: Sendable {
    public let width, height: UInt32
    public let rowStride: UInt64
    public let pixelFormat, alphaMode, storageOrigin: UInt32
    /// Exact RGBA8 bytes in top-to-bottom row order. alphaMode == 1 is the
    /// runtime's legacy destination compositor, not standard straight alpha.
    public let rgba: Data
    public let outputs: [Output]
    public let statistics: Statistics
    public func output(named name: String, owner: UInt32 = 0) -> Output? {
        outputs.first { $0.owner == owner && $0.name.utf8.elementsEqual(name.utf8) }
    }
}

public struct IpropError: Error, Sendable, CustomStringConvertible {
    public let status, reason, stage: UInt32
    public let byteOffset: UInt64
    public let locationFlags, offsetSpace, functionIndex, pc, debugLine: UInt32
    public let message: String
    public let messageTruncated: Bool
    public var description: String { "\(message) (status \(status), reason \(reason), stage \(stage))" }
}

private func checked(_ body: (UnsafeMutablePointer<iprop_error_ex>) -> UInt32) throws {
    var error = iprop_error_ex()
    iprop_error_ex_init(&error, UInt32(MemoryLayout<iprop_error_ex>.size))
    var buffer = [CChar](repeating: 0, count: 8192)
    try buffer.withUnsafeMutableBufferPointer { bytes in
        error.message_buffer = bytes.baseAddress
        error.message_capacity = UInt32(bytes.count)
        let status = body(&error)
        if status != 0 {
            throw IpropError(status: status, reason: error.reason, stage: error.stage,
                byteOffset: error.byte_offset, locationFlags: error.location_flags,
                offsetSpace: error.offset_space, functionIndex: error.function_index,
                pc: error.pc, debugLine: error.debug_line,
                message: String(cString: bytes.baseAddress!), messageTruncated: error.message_truncated != 0)
        }
    }
}

private func string(_ view: iprop_utf8_view) -> String {
    guard view.byte_count > 0, let pointer = view.data else { return "" }
    return String(decoding: UnsafeRawBufferPointer(start: pointer, count: Int(view.byte_count)), as: UTF8.self)
}

/// Owns a validated program. Calls, including close(), serialize on one lock;
/// use independent programs for parallel generation. Results own their copies.
public final class Program: @unchecked Sendable {
    private let lock = NSLock()
    private var handle: OpaquePointer?
    public let metadata: Metadata
    public let inputs: [Input]

    public convenience init(contentsOf url: URL) throws { try self.init(bytes: Data(contentsOf: url)) }

    public init(bytes: Data) throws {
        var library = iprop_library_info_ex()
        iprop_library_info_ex_init(&library, UInt32(MemoryLayout<iprop_library_info_ex>.size))
        try checked { iprop_library_query_ex(&library, $0) }
        guard library.api_major == 0, library.api_minor >= 4,
              library.abi_major == 0, library.abi_minor == 5 else {
            throw IpropError(status: 4, reason: 4000, stage: 0, byteOffset: 0,
                locationFlags: 0, offsetSpace: 0, functionIndex: 0, pc: 0, debugLine: 0,
                message: "InfiniteProps requires C API 0.4 / ABI 0.5", messageTruncated: false)
        }
        var loaded: OpaquePointer?
        // Empty input still supplies a non-null pointer so it is a format error.
        let storage = bytes.isEmpty ? Data([0]) : bytes
        try storage.withUnsafeBytes { data in
            try checked { iprop_program_load_ex(data.bindMemory(to: UInt8.self).baseAddress,
                                                UInt64(bytes.count), &loaded, $0) }
        }
        guard let loaded else { preconditionFailure("C API returned a null program on success") }
        do {
            var meta = iprop_program_info_ex()
            iprop_program_info_ex_init(&meta, UInt32(MemoryLayout<iprop_program_info_ex>.size))
            try checked { iprop_program_query(loaded, &meta, $0) }
            metadata = Metadata(width: meta.size_x, height: meta.size_y,
                originX: meta.origin_x, originY: meta.origin_y, fuel: meta.fuel,
                emissionColor: meta.has_emission == 0 ? nil : RGBA8(meta.emission_color.r,
                    meta.emission_color.g, meta.emission_color.b, meta.emission_color.a),
                emissionOffsetX: meta.emission_offset_x, emissionOffsetY: meta.emission_offset_y,
                emissionRadius: meta.emission_radius, containerFlags: meta.container_flags)
            var count: UInt32 = 0
            try checked { iprop_program_input_count(loaded, &count, $0) }
            var declarations: [Input] = []
            for index in 0..<count {
                var input = iprop_input_info_ex()
                iprop_input_info_ex_init(&input, UInt32(MemoryLayout<iprop_input_info_ex>.size))
                try checked { iprop_program_input_get(loaded, index, &input, $0) }
                let value: InputValue
                switch input.type {
                case 0:
                    let c = input.color_default
                    value = .color(RGBA8(c.r, c.g, c.b, c.a))
                case 1: value = .float(input.f32_default)
                default: value = .uint(input.u32_default)
                }
                declarations.append(Input(index: input.input_index, name: string(input.name), defaultValue: value))
            }
            inputs = declarations
            handle = loaded
        } catch { iprop_program_free(loaded); throw error }
    }

    deinit { close() }
    public func close() {
        lock.lock(); defer { lock.unlock() }
        if let handle { iprop_program_free(handle); self.handle = nil }
    }

    public func generate(seed: UInt32 = 0, width: UInt32 = 0, height: UInt32 = 0,
                         inputs: [String: InputValue] = [:]) throws -> GeneratedImage {
        try generate(seed: seed, width: width, height: height,
                     overrides: inputs.map { InputOverride($0.key, $0.value) })
    }

    public func generate(seed: UInt32 = 0, width: UInt32 = 0, height: UInt32 = 0,
                         overrides: [InputOverride]) throws -> GeneratedImage {
        lock.lock(); defer { lock.unlock() }
        // Passing nil follows the checked API's structured invalid-handle path.
        var parameters: OpaquePointer?
        try checked { iprop_parameters_create(handle, &parameters, $0) }
        defer { iprop_parameters_free(parameters) }
        var seen = Set<Data>()
        for override in overrides {
            let bytes = Array(override.name.utf8)
            guard bytes.count <= UInt32.max, seen.insert(Data(bytes)).inserted else {
                throw IpropError(status: 4, reason: bytes.count > UInt32.max ? 4000 : 4006,
                    stage: 3, byteOffset: 0, locationFlags: 0, offsetSpace: 0,
                    functionIndex: 0, pc: 0, debugLine: 0,
                    message: "Invalid or duplicate input name", messageTruncated: false)
            }
            try bytes.withUnsafeBytes { data in
                let view = iprop_utf8_view(data: data.baseAddress?.assumingMemoryBound(to: CChar.self),
                                           byte_count: UInt32(bytes.count))
                try checked { error in
                    switch override.value {
                    case .float(let f): return iprop_parameters_set_f32_named(parameters, view, f, error)
                    case .uint(let u): return iprop_parameters_set_u32_named(parameters, view, u, error)
                    case .int(let i): return iprop_parameters_set_i32_named(parameters, view, i, error)
                    case .color(let c):
                        return iprop_parameters_set_color_named(parameters, view,
                            iprop_color_rgba8(r: c.r, g: c.g, b: c.b, a: c.a), error)
                    }
                }
            }
        }
        var options = iprop_generate_options_ex()
        iprop_generate_options_ex_init(&options, UInt32(MemoryLayout<iprop_generate_options_ex>.size))
        options.seed = seed; options.size_x = width; options.size_y = height
        var result: OpaquePointer?
        try checked { iprop_generate_ex(handle, &options, parameters, &result, $0) }
        defer { iprop_result_free(result) }
        var image = iprop_image_view_ex()
        iprop_image_view_ex_init(&image, UInt32(MemoryLayout<iprop_image_view_ex>.size))
        try checked { iprop_result_query_image(result, &image, $0) }
        var stats = iprop_result_stats_ex()
        iprop_result_stats_ex_init(&stats, UInt32(MemoryLayout<iprop_result_stats_ex>.size))
        try checked { iprop_result_query_stats(result, &stats, $0) }
        var count: UInt32 = 0
        try checked { iprop_result_output_count(result, &count, $0) }
        var outputs: [Output] = []
        for index in 0..<count {
            var slot = iprop_result_output_view_ex()
            iprop_result_output_view_ex_init(&slot, UInt32(MemoryLayout<iprop_result_output_view_ex>.size))
            try checked { iprop_result_output_get(result, index, &slot, $0) }
            let values = slot.element_count == 0 ? [] : Array(UnsafeBufferPointer(
                start: slot.values, count: Int(slot.element_count) * Int(slot.stride_floats)))
            outputs.append(Output(index: slot.output_index, owner: slot.owner, name: string(slot.name),
                kind: OutputKind(rawValue: slot.kind)!, isList: slot.is_list != 0,
                count: slot.element_count, values: values))
        }
        return GeneratedImage(width: image.width, height: image.height, rowStride: image.row_stride,
            pixelFormat: image.pixel_format, alphaMode: image.alpha_mode, storageOrigin: image.origin,
            rgba: Data(bytes: image.pixels!, count: Int(image.byte_count)), outputs: outputs,
            statistics: Statistics(fuelBudget: stats.fuel_budget, fuelRemaining: stats.fuel_remaining,
                fuelUsed: stats.fuel_used, imageByteCount: stats.image_byte_count,
                outputElementCount: stats.output_element_count))
    }
}
