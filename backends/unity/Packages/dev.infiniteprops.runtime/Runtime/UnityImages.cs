using System;
using UnityEngine;

namespace InfiniteProps
{
    public sealed partial class GeneratedImage
    {
        /// <summary>Creates a caller-owned texture. Raw results remain top-left;
        /// Unity upload rows are flipped here. Display uses Unity's straight
        /// alpha interpretation of the stored legacy-compositor channels.</summary>
        public Texture2D ToTexture2D() => Upload(Rgba, Width, Height, RowStride, false);

        /// <summary>Top-left rows uploaded bottom-up. <paramref name="linear"/>
        /// selects a texture without an sRGB transfer, for data such as normals.</summary>
        internal static Texture2D Upload(byte[] rgba, uint width, uint height, ulong rowStride, bool linear)
        {
            var texture = new Texture2D(checked((int)width), checked((int)height), TextureFormat.RGBA32, false, linear);
            try
            {
                var pixels = new byte[rgba.Length];
                int stride = checked((int)rowStride), rows = checked((int)height);
                for (int row = 0; row < rows; ++row)
                    Buffer.BlockCopy(rgba, row * stride, pixels, (rows - 1 - row) * stride, stride);
                texture.LoadRawTextureData(pixels);
                texture.filterMode = FilterMode.Point;
                texture.wrapMode = TextureWrapMode.Clamp;
                texture.Apply(false, false);
                return texture;
            }
            catch { DestroyOwned(texture); throw; }
        }
        public Sprite ToSprite(Metadata metadata, float pixelsPerUnit = 1)
        {
            if (metadata == null) throw new ArgumentNullException(nameof(metadata));
            if (!(pixelsPerUnit > 0) || float.IsInfinity(pixelsPerUnit)) throw new ArgumentOutOfRangeException(nameof(pixelsPerUnit));
            var texture = ToTexture2D();
            try { return Sprite.Create(texture, new Rect(0, 0, Width, Height),
                new Vector2(metadata.OriginX / Width, 1 - metadata.OriginY / Height), pixelsPerUnit,
                0, SpriteMeshType.FullRect); }
            catch { DestroyOwned(texture); throw; }
        }
        internal static void DestroyOwned(UnityEngine.Object value)
        {
            if (Application.isPlaying) UnityEngine.Object.Destroy(value);
            else UnityEngine.Object.DestroyImmediate(value);
        }
    }

    public sealed partial class AuxiliaryImage
    {
        /// <summary>The name under which Unity's 2D lit sprite shaders sample a normal map.</summary>
        public const string NormalMapProperty = "_NormalMap";

        /// <summary>Creates a caller-owned linear texture with the exact bytes,
        /// rows flipped for Unity like the main image. A normal map keeps its
        /// meaning: green still points up on the displayed sprite, the
        /// convention Unity expects, and alpha 255 lets UnpackNormal read it.</summary>
        public Texture2D ToTexture2D() => GeneratedImage.Upload(Rgba, Width, Height, RowStride, true);

        /// <summary>Binds a normal texture made by <see cref="ToTexture2D"/> to a
        /// sprite renderer through its property block, so a lit 2D material
        /// samples it for this renderer. The caller keeps ownership of the texture.</summary>
        public static void Bind(SpriteRenderer renderer, Texture2D normalMap)
        {
            if (renderer == null) throw new ArgumentNullException(nameof(renderer));
            if (normalMap == null) throw new ArgumentNullException(nameof(normalMap));
            var block = new MaterialPropertyBlock();
            renderer.GetPropertyBlock(block);
            block.SetTexture(NormalMapProperty, normalMap);
            renderer.SetPropertyBlock(block);
        }
    }

}
