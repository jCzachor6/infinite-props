using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using System.Threading.Tasks;
using System.Threading;
using InfiniteProps;

static class Conformance
{
    static void Require(bool value, string message) { if (!value) throw new Exception(message); }
    static void Main(string[] args)
    {
        string path = args[0];
        PartOutputConformance.Run(path);
        ExecutionControls(path);
        NormalMaps(path);
        if (args.Length > 1) InfiniteProps.Tests.PixelCorpus.Run(args[1]);
        var program = new Program(File.ReadAllBytes(Path.Combine(path, "bindings.iprop")));
        Require(program.Metadata.Width == 8 && program.Metadata.Height == 4 && program.Metadata.OriginX == 2, "metadata");
        Require(program.Inputs.Select(x => x.Name).SequenceEqual(new[] { "tint", "amount", "offset" }), "reflection");
        var inputs = new Dictionary<string, InputValue> { ["tint"] = InputValue.Color(18, 52, 86),
            ["amount"] = InputValue.Float(BitConverter.Int32BitsToSingle(unchecked((int)0x80000000))), ["offset"] = InputValue.Int(-7) };
        var result = program.Generate(uint.MaxValue, inputs);
        Require(result.Rgba.SequenceEqual(File.ReadAllBytes(Path.Combine(path, "bindings.rgba"))), "native RGBA oracle");
        Require(result.FindOutput("anchor").Values.Select(BitConverter.SingleToInt32Bits).SequenceEqual(new[] { unchecked((int)0x80000000), unchecked((int)0xc0e00000) }), "signed zero and signed input");
        Require(result.FindOutput("marks").Count == 2 && result.FindOutput("unset").Count == 0, "lists and unset slots");
        Require(result.Statistics.FuelBudget - result.Statistics.FuelRemaining == result.Statistics.FuelUsed && result.Statistics.ImageByteCount == 128, "statistics");
        Require(!program.Generate().Rgba.SequenceEqual(result.Rgba), "parameters reset each call");
        Require(program.Generate(width: 16, height: 8).Rgba.Length == 512, "size override");
        foreach (var item in new[] { ("missing", InputValue.UInt(0), 4005u), ("tint", InputValue.Float(1), 4004u) })
        {
            try { program.Generate(inputs: new Dictionary<string, InputValue> { [item.Item1] = item.Item2 }); throw new Exception("expected error"); }
            catch (IpropException e) { Require(e.Reason == item.Item3 && e.Stage == 3, "structured error"); }
        }
        var expected = program.Generate(17).Rgba;
        Parallel.For(0, 20, i => Require(program.Generate(17).Rgba.SequenceEqual(expected), "serialized calls"));
        program.Dispose(); program.Dispose();
        Require(result.Rgba.Length == 128 && result.FindOutput("box").Values.Length == 4, "result ownership");
        try { program.Generate(); throw new Exception("expected disposed error"); } catch (ObjectDisposedException) { }
        try { using (new Program(Array.Empty<byte>())) { } throw new Exception("expected format error"); } catch (IpropException e) { Require(e.Status == 2, "format status"); }
        using (var raw = new Program(File.ReadAllBytes(Path.Combine(path, "backend-raw-values.iprop"))))
        {
            var image = raw.Generate();
            Require(image.Outputs.Select(x => x.Owner).SequenceEqual(new uint[] { 0, 0, 0, 2, 2, 2 }), "owners");
            Require(image.Outputs.Take(3).Select(x => x.Name).SequenceEqual(new[] { "pł\0suffix", "", "\0c\0\0" }), "byte counted UTF8");
            Require(image.Outputs.Take(3).SelectMany(x => x.Values).Select(x => unchecked((uint)BitConverter.SingleToInt32Bits(x))).SequenceEqual(
                new uint[] { 0x80000000, 1, 0x7fc12345, 0xff800000, 0x7f800000, 0xffc12345, 0x80000001, 0x3f800000, 0x80000000 }), "exact f32 bits");
            Require(image.Outputs.Skip(3).All(x => x.Count == 0), "empty slots preserved");
        }
        using (var names = new Program(File.ReadAllBytes(Path.Combine(path, "binding-unicode.iprop"))))
        {
            Require(names.Inputs[0].Name == "pł\0suffix", "input UTF8/NUL reflection");
            Require(names.Generate(inputs: new Dictionary<string, InputValue> { ["é"] = InputValue.Float(1), ["e\u0301"] = InputValue.Float(2) }).FindOutput("anchor").Values.SequenceEqual(new float[] { 1, 2 }), "no Unicode normalization");
            Require(names.Generate(inputs: new Dictionary<string, InputValue> { ["pł\0suffix"] = InputValue.Color(18, 52, 86) }).Rgba.SequenceEqual(new byte[] { 18, 52, 86, 255 }), "exact named override");
            try { names.Generate(inputs: new Dictionary<string, InputValue> { ["pł\0suffix\0extra"] = InputValue.Color(0, 0, 0) }); throw new Exception("expected unknown name"); }
            catch (IpropException e) { Require(e.Reason == 4005, "no name truncation"); }
        }
        using (var trap = new Program(File.ReadAllBytes(Path.Combine(path, "binding-trap.iprop"))))
        {
            try { trap.Generate(); throw new Exception("expected trap"); }
            catch (IpropException e) { Require(e.Status == 3 && e.Reason == 3008 && e.Stage == 4 && (e.LocationFlags & 8) != 0 && e.DebugLine > 0, "located runtime failure"); }
            Require(trap.Generate(inputs: new Dictionary<string, InputValue> { ["divisor"] = InputValue.Int(1) }).FindOutput("value").Values.SequenceEqual(new float[] { 1, 0 }), "recovery after trap");
        }
        Console.WriteLine("Unity C# / native C API conformance passed");
    }
    // Auxiliary images against the CLI-rendered shared fixtures.
    static void NormalMaps(string path)
    {
        Func<string, byte[]> load = name => File.ReadAllBytes(Path.Combine(path, name));
        using (var program = new Program(load("binding-normal.iprop")))
        {
            Require(program.Images.Count == 1 && program.Images[0].Index == 0 && program.Images[0].Kind == ImageKind.Normal &&
                program.Images[0].Name == "relief", "declared normal output");
            var result = program.Generate(5);
            Require(result.Rgba.SequenceEqual(load("binding-normal.rgba")), "native RGBA oracle with a normal output");
            var normal = result.Normal;
            Require(result.Images.Count == 1 && ReferenceEquals(result.Images[0], normal), "one auxiliary image");
            Require(normal.Index == 0 && normal.Kind == ImageKind.Normal && normal.Source == ImageSource.Authored && normal.Name == "relief", "authored tags");
            Require(normal.Width == 8 && normal.Height == 4 && normal.RowStride == 32, "normal image layout");
            Require(normal.PixelFormat == result.PixelFormat && normal.AlphaMode == 2 && normal.StorageOrigin == result.StorageOrigin, "normal image format");
            Require(normal.Rgba.SequenceEqual(load("binding-normal.normal.rgba")), "native normal map oracle");
            Require(Enumerable.Range(0, normal.Rgba.Length / 4).All(i => normal.Rgba[i * 4 + 3] == 255), "opaque normal image");
            // The seed moves one painted direction; a size override scales the image.
            Require(!program.Generate(6).Normal.Rgba.SequenceEqual(normal.Rgba), "seed-dependent direction");
            var scaled = program.Generate(5, width: 16, height: 8).Normal;
            Require(scaled.Width == 16 && scaled.Height == 8 && scaled.Rgba.Length == 512, "scaled normal image");
            // An authored image wins over a derive request and stays identical.
            var requested = program.Generate(5, deriveNormal: new DerivedNormal(depth: 9)).Normal;
            Require(requested.Source == ImageSource.Authored && requested.Rgba.SequenceEqual(normal.Rgba), "authored wins");
        }
        using (var program = new Program(load("bindings.iprop")))
        {
            Require(program.Images.Count == 0, "no declared image");
            var plain = program.Generate(5);
            Require(plain.Images.Count == 0 && plain.Normal == null, "derived images are opt-in");
            foreach (var item in new[] { (new DerivedNormal(), "bindings-derived"),
                                         (new DerivedNormal(2, 0.5f, 0.25f), "bindings-derived-custom") })
            {
                var result = program.Generate(5, deriveNormal: item.Item1);
                var normal = result.Normal;
                Require(normal.Kind == ImageKind.Normal && normal.Source == ImageSource.Derived && normal.Name == "normal", "derived tags");
                Require(normal.Rgba.SequenceEqual(load(item.Item2 + ".normal.rgba")), "native derived normal oracle: " + item.Item2);
                Require(normal.Width == 8 && normal.Height == 4 && normal.AlphaMode == 2, "derived layout");
                // The request leaves the main image and fuel alone and spends work.
                Require(result.Rgba.SequenceEqual(plain.Rgba) && result.Statistics.FuelUsed == plain.Statistics.FuelUsed, "main image unchanged");
                Require(result.Statistics.WorkUsed > plain.Statistics.WorkUsed, "derivation spends work");
            }
            ulong used = program.Generate(5, deriveNormal: new DerivedNormal()).Statistics.WorkUsed;
            Require(program.Generate(5, limits: new ExecutionLimits(used), deriveNormal: new DerivedNormal()).Statistics.WorkRemaining == 0, "exact work");
            try { program.Generate(5, limits: new ExecutionLimits(used - 1), deriveNormal: new DerivedNormal()); throw new Exception("work limit was ignored"); }
            catch (IpropException failure) { Require(failure.Reason == 3015 && failure.Stage == 4, "work limit during derivation"); }
            foreach (var request in new[] { new DerivedNormal(depth: -1), new DerivedNormal(depth: float.PositiveInfinity), new DerivedNormal(rounding: 64.5f),
                                            new DerivedNormal(rounding: -0.5f), new DerivedNormal(detail: 1.5f), new DerivedNormal(detail: float.NaN) })
            {
                try { program.Generate(5, deriveNormal: request); throw new Exception("expected an argument error"); }
                catch (IpropException failure) { Require(failure.Status == 4 && failure.Stage == 3 && failure.Message.Contains("out of range"), "invalid derive request"); }
            }
            Require(program.Generate(5).Normal == null, "recovery after argument errors");
        }
        Console.WriteLine("Production C# normal maps: PASS authored and derived images against the CLI, declarations, work limits and argument errors");
    }
    static void ExecutionControls(string path)
    {
        using (var program = new Program(File.ReadAllBytes(Path.Combine(path, "execution-slow.iprop"))))
        using (var cancel = new CancellationTokenSource())
        {
            cancel.CancelAfter(10);
            try { program.Generate(limits: new ExecutionLimits(1000000000, cancellation: cancel.Token)); throw new Exception("active cancellation was ignored"); }
            catch (IpropException failure)
            {
                Require(failure.Reason == 3016 && failure.Stage == 4 && (failure.LocationFlags & 4) != 0, "cancel inside native scan");
            }
            cancel.Cancel();
            for (int i = 0; i < 32; ++i)
            {
                try { program.Generate(limits: new ExecutionLimits(cancellation: cancel.Token)); throw new Exception("pre-cancel was ignored"); }
                catch (IpropException failure) { Require(failure.Reason == 3016, "pre-cancel reason"); }
            }
            Require(program.Generate(inputs: new Dictionary<string, InputValue> { ["enabled"] = InputValue.Int(0) }).Width == 1, "recovery after cancel");
        }
        Console.WriteLine("Production C# execution limits: PASS 23 exact typed budgets, deadline, concurrent cancellation and recovery");
    }

}
