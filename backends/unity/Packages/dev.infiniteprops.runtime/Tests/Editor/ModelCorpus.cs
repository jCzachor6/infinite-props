using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

namespace InfiniteProps.Tests
{
    public static class ModelCorpus
    {
        static byte[] Blob(BinaryReader r) => r.ReadBytes(checked((int)r.ReadUInt32()));
        static string Text(BinaryReader r) => Encoding.UTF8.GetString(Blob(r));
        static ulong Hash(byte[] bytes)
        {
            ulong h = 0xcbf29ce484222325UL;
            foreach (byte b in bytes) h = unchecked((h ^ b) * 0x100000001b3UL);
            return h;
        }
        static byte[] Bytes(Array data)
        {
            var bytes = new byte[Buffer.ByteLength(data)];
            Buffer.BlockCopy(data, 0, bytes, 0, bytes.Length);
            return bytes;
        }
        public static void Run(string path)
        {
            var data = File.ReadAllBytes(Path.Combine(path, "model-corpus.bin"));
            using (var sha = SHA256.Create())
            {
                var digest = BitConverter.ToString(sha.ComputeHash(data)).Replace("-", "").ToLowerInvariant();
                var manifest = File.ReadAllText(Path.Combine(path, "model-corpus.json"));
                if (!Regex.IsMatch(manifest, "\\x22sha256\\x22\\s*:\\s*\\x22" + digest + "\\x22"))
                    throw new InvalidDataException("Model corpus SHA-256");
            }
            using (var r = new BinaryReader(new MemoryStream(data)))
            {
                if (Encoding.ASCII.GetString(r.ReadBytes(8)) != "IPMCRP01") throw new InvalidDataException();
                var models = new List<Model>();
                try
                {
                    uint programs = r.ReadUInt32();
                    for (int i = 0; i < programs; ++i) models.Add(new Model(Blob(r)));
                    uint count = r.ReadUInt32();
                    for (int i = 0; i < count; ++i)
                    {
                        string label = Text(r);
                        void Check(bool equal) { if (!equal) throw new InvalidDataException(label); }
                        var model = models[(int)r.ReadUInt32()];
                        uint seed = r.ReadUInt32(), lod = r.ReadUInt32(), n = r.ReadUInt32();
                        var inputs = new Dictionary<string, InputValue>();
                        for (int j = 0; j < n; ++j)
                        {
                            var name = Text(r);
                            uint kind = r.ReadUInt32(), bits = r.ReadUInt32();
                            inputs[name] = kind == 0 ? InputValue.Color((byte)bits, (byte)(bits >> 8), (byte)(bits >> 16), (byte)(bits >> 24))
                                : kind == 1 ? InputValue.Float(BitConverter.Int32BitsToSingle((int)bits)) : InputValue.UInt(bits);
                        }
                        var m = model.Generate(seed, inputs, lod);
                        foreach (var f in m.Size.Concat(new[] { m.UnitsPerMeter }).Concat(m.BoundsMin).Concat(m.BoundsMax))
                            Check(BitConverter.SingleToInt32Bits(f) == r.ReadInt32());
                        var s = m.Statistics;
                        Check(s.VertexCount == r.ReadUInt32() && s.TriangleCount == r.ReadUInt32());
                        foreach (var value in new[] { s.FuelBudget, s.FuelUsed, s.WorkBudget, s.WorkUsed }) Check(value == r.ReadUInt64());
                        Check(m.Textures.Length == r.ReadUInt32());
                        foreach (var t in m.Textures)
                        {
                            Check(t.Key == r.ReadUInt64() && t.Name == Text(r));
                            foreach (uint value in new[] {t.Width,t.Height,t.AssetWidth,t.AssetHeight,t.Repeats ? 1u : 0u}) Check(value == r.ReadUInt32());
                            Check(Hash(t.Rgba) == r.ReadUInt64() && t.Images.Length == r.ReadUInt32());
                            foreach (var a in t.Images) Check((uint)a.Kind == r.ReadUInt32() && a.Name == Text(r) && Hash(a.Rgba) == r.ReadUInt64());
                        }
                        Check(m.Surfaces.Length == r.ReadUInt32());
                        foreach (var surface in m.Surfaces)
                        {
                            Check(surface.TextureIndex == r.ReadUInt32());
                            foreach (var f in new[] { surface.Roughness, surface.Metallic, surface.Emission })
                                Check(BitConverter.SingleToInt32Bits(f) == r.ReadInt32());
                            Check(surface.AlphaMode == r.ReadUInt32() && BitConverter.SingleToInt32Bits(surface.AlphaCutoff) == r.ReadInt32());
                            Check(surface.TwoSided == (r.ReadUInt32() != 0) && surface.Crisp == (r.ReadUInt32() != 0));
                            foreach (var array in new Array[] { surface.Positions, surface.Normals, surface.Tangents, surface.Uvs, surface.Indices })
                                Check(Bytes(array).SequenceEqual(Blob(r)));
                        }
                        Check(m.Outputs.Length == r.ReadUInt32());
                        foreach (var o in m.Outputs)
                            Check(o.Name == Text(r) && o.Kind == r.ReadUInt32() && o.IsList == (r.ReadUInt32() != 0) &&
                                  o.ValueCount == r.ReadUInt32() && Bytes(o.Values).SequenceEqual(Blob(r)));
                    }
                    if (r.BaseStream.Position != data.Length) throw new InvalidDataException("Trailing model corpus data");
                }
                finally { foreach (var model in models) model.Dispose(); }
            }
        }
    }
}
