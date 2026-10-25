using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using System.Threading.Tasks;
using InfiniteProps;

static class Conformance
{
    static void Require(bool value, string message) { if (!value) throw new Exception(message); }
    static void Main(string[] args)
    {
        string path = args[0];
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
}
