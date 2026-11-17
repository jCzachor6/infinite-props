import Foundation
import CoreGraphics
import Metal
import SpriteKit

public enum ImagePresentationError: Error { case allocation }

private func cgImage(_ rgba: Data, _ width: UInt32, _ height: UInt32, _ rowStride: UInt64,
                     space: CFString, alpha: CGImageAlphaInfo) throws -> CGImage {
    guard let provider = CGDataProvider(data: rgba as CFData),
          let image = CGImage(width: Int(width), height: Int(height), bitsPerComponent: 8,
            bitsPerPixel: 32, bytesPerRow: Int(rowStride), space: CGColorSpace(name: space)!,
            bitmapInfo: CGBitmapInfo.byteOrder32Big.union(CGBitmapInfo(rawValue: alpha.rawValue)),
            provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
    else { throw ImagePresentationError.allocation }
    return image
}

private func metalTexture(_ rgba: Data, _ width: UInt32, _ height: UInt32, _ rowStride: UInt64,
                          device: MTLDevice) throws -> MTLTexture {
    let descriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba8Unorm,
        width: Int(width), height: Int(height), mipmapped: false)
    descriptor.usage = .shaderRead
    #if os(macOS)
    descriptor.storageMode = device.hasUnifiedMemory ? .shared : .managed
    #else
    descriptor.storageMode = .shared
    #endif
    guard let texture = device.makeTexture(descriptor: descriptor) else {
        throw ImagePresentationError.allocation
    }
    rgba.withUnsafeBytes { bytes in
        texture.replace(region: MTLRegionMake2D(0, 0, Int(width), Int(height)),
            mipmapLevel: 0, withBytes: bytes.baseAddress!, bytesPerRow: Int(rowStride))
    }
    return texture
}

extension GeneratedImage {
    /// Presentation interprets stored channels as straight RGBA. Raw rgba and
    /// alphaMode stay unchanged; legacy-composited pixels cannot be unblended.
    public func makeCGImage() throws -> CGImage {
        try cgImage(rgba, width, height, rowStride, space: CGColorSpace.sRGB, alpha: .last)
    }

    public func makeSKTexture() throws -> SKTexture {
        let texture = SKTexture(cgImage: try makeCGImage())
        texture.filteringMode = .nearest
        return texture
    }

    /// Uploads exact stored RGBA bytes. Select blending in the host renderer.
    public func makeMetalTexture(device: MTLDevice) throws -> MTLTexture {
        try metalTexture(rgba, width, height, rowStride, device: device)
    }
}

extension AuxiliaryImage {
    /// The stored bytes as an opaque image: a normal, roughness, metallic or
    /// AO map is data, in a linear colour space with no transfer function; an
    /// emission map is a colour, in sRGB like the main image. For saving or
    /// inspection.
    public func makeCGImage() throws -> CGImage {
        try cgImage(rgba, width, height, rowStride, space: kind == .emission ? CGColorSpace.sRGB : CGColorSpace.linearSRGB,
                    alpha: .noneSkipLast)
    }

    /// A texture for `SKSpriteNode.normalTexture`, which SpriteKit lights with
    /// its `SKLightNode`s. Stored directions are x right, y up, z toward the viewer.
    public func makeSKTexture() throws -> SKTexture {
        let texture = SKTexture(cgImage: try makeCGImage())
        texture.filteringMode = .nearest
        return texture
    }

    /// Uploads the exact bytes as `.rgba8Unorm`. A shader decodes a normal map
    /// sample as `rgb * 2 - 1` and normalizes it, reads a scalar map from red,
    /// and treats an emission map like the main image's colours.
    public func makeMetalTexture(device: MTLDevice) throws -> MTLTexture {
        try metalTexture(rgba, width, height, rowStride, device: device)
    }
}
