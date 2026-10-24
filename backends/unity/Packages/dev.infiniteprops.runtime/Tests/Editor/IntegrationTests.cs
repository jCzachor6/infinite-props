using System;
using System.IO;
using NUnit.Framework;
using UnityEditor;
using UnityEngine;

namespace InfiniteProps.Tests
{
    public sealed class IntegrationTests
    {
        private static byte[] Fixture()
        {
            string directory = Environment.GetEnvironmentVariable("IPROP_FIXTURES");
            Assert.That(directory, Is.Not.Null.And.Not.Empty, "Set IPROP_FIXTURES before launching Unity");
            return File.ReadAllBytes(Path.Combine(directory, "bindings.iprop"));
        }

        [Test]
        public void TextureRowsAndSpritePivotMatchTheNativeImage()
        {
            using (var program = new Program(Fixture()))
            {
                var image = program.Generate(7);
                var texture = image.ToTexture2D();
                var sprite = image.ToSprite(program.Metadata);
                try
                {
                    Assert.AreEqual(new Vector2(2, 1), sprite.pivot);
                    var pixels = texture.GetPixels32();
                    for (int y = 0; y < image.Height; ++y) for (int x = 0; x < image.Width; ++x)
                    {
                        int at = (y * (int)image.Width + x) * 4;
                        var expected = new Color32(image.Rgba[at], image.Rgba[at + 1], image.Rgba[at + 2], image.Rgba[at + 3]);
                        Assert.AreEqual(expected, pixels[((int)image.Height - 1 - y) * (int)image.Width + x]);
                    }
                }
                finally
                {
                    UnityEngine.Object.DestroyImmediate(texture);
                    UnityEngine.Object.DestroyImmediate(sprite.texture);
                    UnityEngine.Object.DestroyImmediate(sprite);
                }
            }
        }

        [Test]
        public void ImporterRetainsTheCompiledProgramAndCreatesAPreview()
        {
            string name = "InfinitePropsConformance-" + Guid.NewGuid().ToString("N");
            string folder = "Assets/" + name;
            AssetDatabase.CreateFolder("Assets", name);
            try
            {
                string assetPath = folder + "/fixture.iprop";
                File.WriteAllBytes(assetPath, Fixture());
                AssetDatabase.ImportAsset(assetPath, ImportAssetOptions.ForceSynchronousImport);
                var asset = AssetDatabase.LoadAssetAtPath<IpropAsset>(assetPath);
                Assert.IsNotNull(asset);
                using (var program = asset.Load()) Assert.AreEqual(128, program.Generate().Rgba.Length);
                Assert.IsTrue(Array.Exists(AssetDatabase.LoadAllAssetsAtPath(assetPath), item => item is Texture2D));
            }
            finally { AssetDatabase.DeleteAsset(folder); }
        }
    }
}
