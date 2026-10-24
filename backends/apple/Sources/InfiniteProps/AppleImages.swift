import Foundation
import CoreGraphics
import Metal
import SpriteKit

public enum ImagePresentationError: Error { case allocation }

extension GeneratedImage {
    /// Presentation interprets stored channels as straight RGBA. Raw rgba and
    /// alphaMode stay unchanged; legacy-composited pixels cannot be unblended.
    public func makeCGImage() throws -> CGImage {
        guard let provider = CGDataProvider(data: rgba as CFData),
              let image = CGImage(width: Int(width), height: Int(height), bitsPerComponent: 8,
                bitsPerPixel: 32, bytesPerRow: Int(rowStride),
                space: CGColorSpace(name: CGColorSpace.sRGB)!,
                bitmapInfo: CGBitmapInfo.byteOrder32Big.union(CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue)),
                provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
        else { throw ImagePresentationError.allocation }
        return image
    }

    public func makeSKTexture() throws -> SKTexture {
        let texture = SKTexture(cgImage: try makeCGImage())
        texture.filteringMode = .nearest
        return texture
    }

    /// Uploads exact stored RGBA bytes. Select blending in the host renderer.
    public func makeMetalTexture(device: MTLDevice) throws -> MTLTexture {
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
}
