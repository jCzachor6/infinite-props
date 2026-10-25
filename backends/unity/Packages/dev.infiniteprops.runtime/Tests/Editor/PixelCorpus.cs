using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Security.Cryptography;
using System.Collections.Generic;

namespace InfiniteProps.Tests
{
    // Shared by the real Unity Editor tests and the standalone production C# test.
    public static class PixelCorpus
    {
        static byte[] Blob(BinaryReader reader) {
            int count = checked((int)reader.ReadUInt32());
            byte[] bytes = reader.ReadBytes(count);
            Require(bytes.Length == count, "Truncated corpus"); return bytes;
        }
        static void Require(bool condition, string message) { if (!condition) throw new Exception(message); }
        public static int Run(string directory, Action<GeneratedImage, byte[], int> texture = null)
        {
            byte[] data = File.ReadAllBytes(Path.Combine(directory, "pixel-corpus.bin"));
            string hash;
            using (var sha = SHA256.Create()) hash = BitConverter.ToString(sha.ComputeHash(data)).Replace("-", "").ToLowerInvariant();
            // Verify the exact artifact shared with all other backend jobs.
            Require(File.ReadAllText(Path.Combine(directory, "pixel-corpus.json")).Contains(hash), "Corpus SHA-256 mismatch");
            using (var reader = new BinaryReader(new MemoryStream(data), Encoding.UTF8))
            {
                Require(Encoding.ASCII.GetString(reader.ReadBytes(8)) == "IPXCRP01", "Corpus version");
                var programs = new byte[checked((int)reader.ReadUInt32())][];
                for (int i = 0; i < programs.Length; ++i) programs[i] = Blob(reader);
                int count = checked((int)reader.ReadUInt32()); Require(count > 0, "Empty corpus");
                for (int i = 0; i < count; ++i)
                using (var program = new Program(programs[checked((int)reader.ReadUInt32())]))
                {
                    uint seed = reader.ReadUInt32(), width = reader.ReadUInt32(), height = reader.ReadUInt32();
                    var inputs = new Dictionary<string, InputValue>(StringComparer.Ordinal);
                    uint inputCount = reader.ReadUInt32();
                    for (int j = 0; j < inputCount; ++j)
                    {
                        string name = Encoding.UTF8.GetString(Blob(reader));
                        uint tag = reader.ReadUInt32(), bits = reader.ReadUInt32(), integer = reader.ReadUInt32();
                        inputs.Add(name, tag == 0 ? InputValue.Color((byte)bits, (byte)(bits >> 8), (byte)(bits >> 16), (byte)(bits >> 24)) :
                            program.Inputs.First(x => x.Name == name).DefaultValue.Type == InputKind.Float ?
                            InputValue.Float(BitConverter.ToSingle(BitConverter.GetBytes(bits), 0)) : InputValue.UInt(integer));
                    }
                    uint expectedWidth = reader.ReadUInt32(), expectedHeight = reader.ReadUInt32();
                    byte[] expected = Blob(reader);
                    var image = program.Generate(seed, inputs, width, height);
                    string label = "Corpus case " + i + " seed " + seed;
                    Require(image.Width == expectedWidth && image.Height == expectedHeight, label + ": size");
                    Require(image.Rgba.SequenceEqual(expected), label + ": RGBA");
                    var outputs = image.Outputs;
                    Require(outputs.Count == reader.ReadUInt32(), label + ": outputs");
                    foreach (var output in outputs)
                    {
                        Require(Encoding.UTF8.GetBytes(output.Name).SequenceEqual(Blob(reader)), label + ": name");
                        reader.ReadUInt32(); // Reserved; WASM does not expose owner IDs.
                        Require((uint)output.Kind == reader.ReadUInt32() &&
                            (output.IsList ? 1u : 0u) == reader.ReadUInt32() && output.Count == reader.ReadUInt32(), label + ": slot");
                        Require(output.Values.Length == reader.ReadUInt32(), label + ": value count");
                        // Inspect storage without loading a floating-point register:
                        // Unity's embedded Mono can quiet a signaling NaN when a
                        // float is passed to BitConverter.GetBytes(float).
                        var raw = new byte[checked(output.Values.Length * 4)];
                        Buffer.BlockCopy(output.Values, 0, raw, 0, raw.Length);
                        for (int leaf = 0; leaf < output.Values.Length; ++leaf)
                        {
                            uint actual = BitConverter.ToUInt32(raw, leaf * 4), expectedBits = reader.ReadUInt32();
                            Require(actual == expectedBits, label + ": f32 storage bits at " + leaf +
                                ", actual=" + actual.ToString("x8") + ", expected=" + expectedBits.ToString("x8"));
                        }
                    }
                    texture?.Invoke(image, expected, i);
                }
                Require(reader.BaseStream.Position == reader.BaseStream.Length, "Unread corpus cases");
                Console.WriteLine("C# pixel corpus: " + count + " cases; SHA-256 " + hash);
                return count;
            }
        }
    }
}
