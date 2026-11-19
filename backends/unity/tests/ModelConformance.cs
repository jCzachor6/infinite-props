using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.Threading;
using System.Text.RegularExpressions;
using System.Linq;
using System.Threading.Tasks;
using InfiniteProps;
using InfiniteProps.Tests;

static class ModelConformance
{
    internal static void Run(string path)
    {
        if (IntPtr.Size == 8)
        {
            using var reader = new StreamReader(typeof(ModelConformance).Assembly.GetManifestResourceStream("ModelLayoutPins"));
            string pins = reader.ReadToEnd();
            string Pascal(string name) => string.Concat(name.Split('_').Select(w => char.ToUpperInvariant(w[0]) + w.Substring(1)));
            Type NativeType(string name) => typeof(Native3D).GetNestedType(name == "library_info" ? "LibraryInfo3D" : Pascal(name),
                System.Reflection.BindingFlags.NonPublic) ?? throw new Exception("Missing native type " + name);
            foreach (Match pin in Regex.Matches(pins, @"LAYOUT\(iprop3d_(\w+), (\d+)\)"))
            {
                var type = NativeType(pin.Groups[1].Value);
                if (Marshal.SizeOf(type) != int.Parse(pin.Groups[2].Value)) throw new Exception("Invalid size of " + type);
            }
            foreach (Match pin in Regex.Matches(pins, @"OFFSET\(iprop3d_(\w+), (\w+), (\d+)\)"))
            {
                var type = NativeType(pin.Groups[1].Value);
                string field = Pascal(pin.Groups[2].Value);
                if (field == "BoundsMin" || field == "BoundsMax" || field == "Size") field += "X";
                if (Marshal.OffsetOf(type, field).ToInt32() != int.Parse(pin.Groups[3].Value))
                    throw new Exception("Invalid offset of " + type.Name + "." + field);
            }
        }
        ModelCorpus.Run(path);
        using (var model = new Model(File.ReadAllBytes(Path.Combine(path, "model-crate.iprop3d"))))
        {
            void Expect(uint reason, Action body)
            {
                try { body(); throw new Exception("Expected model failure " + reason); }
                catch (IpropException e) when (e.Reason == reason) {}
                model.Generate();
            }
            Expect(3015, () => model.Generate(limits: new ExecutionLimits(workLimit: 200)));
            Expect(3016, () => model.Generate(limits: new ExecutionLimits(cancellation: new CancellationToken(true))));
            Expect(4005, () => model.Generate(inputs: new Dictionary<string,InputValue> { ["missing"] = InputValue.Float(1) }));
            Expect(4004, () => model.Generate(inputs: new Dictionary<string,InputValue> { ["wide"] = InputValue.UInt(1) }));
            Expect(3010, () => model.Generate(inputs: new Dictionary<string,InputValue> { ["wide"] = InputValue.Float(0) }));
            if (!model.Inputs.Select(x => x.Name).SequenceEqual(new[] {"wide","stain","extra","rounding"}) ||
                model.Metadata.SizeX != 12 || model.Metadata.SizeY != 10 || model.Metadata.SizeZ != 8 ||
                model.Metadata.UnitsPerMeter != 50 || model.SkinPrograms.Single().Name != "tile")
                throw new Exception("Model declarations");
            using (var tile = new Program(File.ReadAllBytes(Path.Combine(path, "model-tile.iprop"))))
            {
                var expected = tile.Generate(77, new Dictionary<string,InputValue> { ["tint"] = InputValue.Color(51,102,153) }, 32, 16);
                var texture = model.Generate(5).Textures[0];
                if (!expected.Rgba.SequenceEqual(texture.Rgba) || !expected.Images.Single().Rgba.SequenceEqual(texture.Images.Single().Rgba))
                    throw new Exception("Model skin differs from 2D generation");
            }
            var cache = new TextureCache();
            var cold = model.Generate(textures: cache);
            var warm = model.Generate(textures: cache);
            if (!warm.Textures.All(t => t.Reused && ReferenceEquals(t.Rgba, cold.Textures[t.Index].Rgba)))
                throw new Exception("Model texture reuse failed");
            Parallel.For(0, 48, i => {
                uint seed = (uint)(100 + i % 8);
                var generated = model.Generate(seed, textures: cache);
                if (!generated.Surfaces[0].Positions.SequenceEqual(model.Generate(seed).Surfaces[0].Positions))
                    throw new Exception("Concurrent model mismatch");
            });
            try { model.Generate(lod: 8); throw new Exception("Accepted invalid LOD"); }
            catch (IpropException e) when (e.Status == 4) {}
            model.Dispose();
            if (cold.Textures[0].Rgba.Length == 0) throw new Exception("Model texture lifetime");
            try { model.Generate(); throw new Exception("Disposed model generated"); }
            catch (ObjectDisposedException) {}
        }
    }
}
