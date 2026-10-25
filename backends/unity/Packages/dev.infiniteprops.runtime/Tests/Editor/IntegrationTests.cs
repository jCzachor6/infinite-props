using System;
using System.IO;
using NUnit.Framework;
using UnityEditor;
using UnityEngine;
using UnityEngine.Rendering;

namespace InfiniteProps.Tests
{
    public sealed class IntegrationTests
    {
        private static byte[] Fixture()
        {
            string directory = Environment.GetEnvironmentVariable("IPROP_FIXTURES");
            if (string.IsNullOrEmpty(directory)) directory = Path.GetFullPath(Path.Combine(Application.dataPath, "../PixelCorpus"));
            return File.ReadAllBytes(Path.Combine(directory, "bindings.iprop"));
        }

        [Test]
        public void EntireSharedCorpusSurvivesTextureUploadAndGpuReadback()
        {
            Assert.AreNotEqual(GraphicsDeviceType.Null, SystemInfo.graphicsDeviceType, "A graphics device is required; do not run with -nographics");
            var arguments = Environment.GetCommandLineArgs();
            int colorFlag = Array.IndexOf(arguments, "-ipropColorSpace");
            if (colorFlag >= 0)
            {
                Assert.Less(colorFlag + 1, arguments.Length, "Missing -ipropColorSpace value");
                Assert.AreEqual(arguments[colorFlag + 1], QualitySettings.activeColorSpace.ToString(), "The requested project color space must be active");
            }
            Debug.Log("Unity pixel corpus: " + QualitySettings.activeColorSpace + ", " + SystemInfo.graphicsDeviceType + ", " + SystemInfo.graphicsDeviceName);
            string directory = Environment.GetEnvironmentVariable("IPROP_PIXEL_CORPUS_DIR");
            if (string.IsNullOrEmpty(directory)) directory = Path.GetFullPath(Path.Combine(Application.dataPath, "../PixelCorpus"));
            var shader = Shader.Find("Hidden/InfiniteProps/PixelCopy");
            Assert.IsNotNull(shader);
            Assert.IsTrue(shader.isSupported);
            var material = new Material(shader);
            Assert.IsTrue(material.HasProperty("_MainTex"), "Blit requires an explicit source texture property");
            try
            {
                PixelCorpus.Run(directory, (image, expected, index) => {
                    var texture = image.ToTexture2D();
                    var descriptor = new RenderTextureDescriptor((int)image.Width, (int)image.Height) {
                        graphicsFormat = texture.graphicsFormat, depthBufferBits = 0, msaaSamples = 1, useMipMap = false
                    };
                    var target = new RenderTexture(descriptor);
                    var readback = new Texture2D((int)image.Width, (int)image.Height, TextureFormat.RGBA32, false, false);
                    var previous = RenderTexture.active;
                    bool previousSrgb = GL.sRGBWrite;
                    try
                    {
                        AssertTextureBytes(texture, expected, image, index);
                        Assert.IsTrue(target.Create());
                        GL.sRGBWrite = true;
                        Graphics.Blit(texture, target, material);
                        RenderTexture.active = target;
                        readback.ReadPixels(new Rect(0, 0, image.Width, image.Height), 0, 0, false);
                        readback.Apply(false, false);
                        AssertTextureBytes(readback, expected, image, index);
                    }
                    finally
                    {
                        GL.sRGBWrite = previousSrgb;
                        RenderTexture.active = previous;
                        UnityEngine.Object.DestroyImmediate(readback);
                        UnityEngine.Object.DestroyImmediate(target);
                        UnityEngine.Object.DestroyImmediate(texture);
                    }
                });
            }
            finally { UnityEngine.Object.DestroyImmediate(material); }
        }

        static void AssertTextureBytes(Texture2D texture, byte[] expected, GeneratedImage image, int index)
        {
            var actual = texture.GetRawTextureData<byte>();
            int stride = checked((int)image.Width * 4);
            Assert.AreEqual(expected.Length, actual.Length, "Texture length case " + index);
            for (int y = 0; y < image.Height; ++y) for (int x = 0; x < stride; ++x)
                if (expected[y * stride + x] != actual[((int)image.Height - 1 - y) * stride + x])
                    Assert.Fail("Texture RGBA case " + index + ", row " + y + ", byte " + x +
                        ": actual=" + actual[((int)image.Height - 1 - y) * stride + x] + ", expected=" + expected[y * stride + x] +
                        ", format=" + texture.graphicsFormat + ", colorSpace=" + QualitySettings.activeColorSpace);
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
