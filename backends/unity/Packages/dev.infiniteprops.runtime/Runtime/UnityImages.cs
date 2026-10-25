using System;
using UnityEngine;

namespace InfiniteProps
{
    public sealed partial class GeneratedImage
    {
        /// <summary>Creates a caller-owned texture. Raw results remain top-left;
        /// Unity upload rows are flipped here. Display uses Unity's straight
        /// alpha interpretation of the stored legacy-compositor channels.</summary>
        public Texture2D ToTexture2D()
        {
            var texture = new Texture2D(checked((int)Width), checked((int)Height), TextureFormat.RGBA32, false, false);
            try
            {
                var pixels = new byte[Rgba.Length];
                int stride = checked((int)RowStride);
                for (int row = 0; row < Height; ++row)
                    Buffer.BlockCopy(Rgba, row * stride, pixels, (checked((int)Height) - 1 - row) * stride, stride);
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

}
