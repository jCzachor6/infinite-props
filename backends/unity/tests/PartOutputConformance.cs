using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Nodes;
using InfiniteProps;

static class PartOutputConformance
{
    static uint Bits(float f) => unchecked((uint)BitConverter.SingleToInt32Bits(f));
    static uint[] PointBits(Point p) => new[] { Bits(p.X), Bits(p.Y) };
    static object Observe(GeneratedImage result)
    {
        var outputs = result.Outputs.Select((s, i) => {
            if (s.Index != i) throw new Exception("slot index");
            int stride = new[] { 2, 4, 3, 0 }[(int)s.Kind];
            if (s.Count != (stride == 0 ? s.Parts.Count : s.Values.Length / stride)) throw new Exception("slot count");
            var parts = s.Parts.Select(p => {
                if (!ReferenceEquals(result.Resource(p.ShapeResource), result.Resources[(int)p.ShapeResource - 1])) throw new Exception("resource identity");
                if (p.TextureResource != 0 && result.Resource(p.TextureResource) == null) throw new Exception("texture ID");
                return new { slot = p.OutputIndex, entry = p.ElementIndex, id = p.Id, source_id = p.SourceId,
                    shape = new[] { p.ShapeKind, p.ShapeResource, p.RegionIndex },
                    appearance = new uint[] { p.AppearanceKind, p.TextureResource, p.Tint[0], p.Tint[1], p.Tint[2], p.Tint[3] },
                    source_mapping = new long[] { p.SourceD4, p.SourceOriginX, p.SourceOriginY },
                    texture_mapping = new long[] { p.TextureD4, p.TextureOriginX, p.TextureOriginY },
                    transform = new long[] { p.D4, p.TranslationX, p.TranslationY },
                    source_scale = PointBits(p.SourceScale), target_scale = PointBits(p.TargetScale), anchor = PointBits(p.SourceAnchor),
                    center = PointBits(p.Center), bounds_origin = PointBits(p.BoundsOrigin), bounds_size = PointBits(p.BoundsSize) };
            }).ToArray();
            var values = stride == 0 ? Array.Empty<uint[]>() : s.Values.Select(Bits).Chunk(stride).ToArray();
            return new { name = s.Name, owner = s.Owner, kind = (uint)s.Kind, list = s.IsList, values, parts };
        }).ToArray();
        var resources = result.Resources.Select((r, i) => {
            if (r.Id != i + 1 || r.ByteCount != (ulong)r.Data.LongLength) throw new Exception("resource metadata");
            if (r.Kind == 2) return (object)new { kind = "raster", format = r.PixelFormat, size = new[] { r.Width, r.Height },
                scale = PointBits(r.Scale), stride = r.RowStride, bytes = r.Data.Select(v => (uint)v).ToArray() };
            if (r.Kind != 1) throw new Exception("resource kind");
            return (object)new { kind = "geometry", vertex_count = r.VertexCount, edge_count = r.EdgeCount,
                rings = r.Rings.Select((v, index) => {
                    if (v.Index != index) throw new Exception("ring index");
                    return new { parent = v.ParentRing, hole = v.IsHole, vertices = v.Vertices.Select(PointBits).ToArray() };
                }).ToArray(),
                regions = r.Regions.Select((v, index) => {
                    if (v.Index != index) throw new Exception("region index");
                    return new { outer = v.OuterRing, holes = v.Holes.ToArray(), source_id = v.SourceId, area = Bits(v.Area),
                        center = PointBits(v.Center), bounds_origin = PointBits(v.BoundsOrigin), bounds_size = PointBits(v.BoundsSize) };
                }).ToArray() };
        }).ToArray();
        return new { schema = 1, size = new[] { result.Width, result.Height }, rgba = result.Rgba.Select(v => (uint)v).ToArray(), outputs, resources };
    }
    static void Equal(GeneratedImage result, JsonNode expected, string name)
    {
        var actual = JsonNode.Parse(JsonSerializer.Serialize(Observe(result)));
        if (!JsonNode.DeepEquals(actual, expected)) throw new Exception("Complete C++/C part result mismatch: " + name);
    }
    internal static void Run(string path)
    {
        var cases = JsonNode.Parse(File.ReadAllText(Path.Combine(path, "part-bindings.json"))).AsArray();
        if (cases.Count != 23) throw new Exception("part fixture count");
        foreach (var item in cases)
        {
            var dir = Path.Combine(path, item["directory"].GetValue<string>());
            var expected = JsonNode.Parse(File.ReadAllText(Path.Combine(dir, "expected.json")));
            var seed = item["seed"].GetValue<uint>(); var width = item["size"][0].GetValue<uint>(); var height = item["size"][1].GetValue<uint>();
            GeneratedImage result;
            using (var program = new Program(File.ReadAllBytes(Path.Combine(dir, "root.iprop"))))
            {
                result = program.Generate(seed, width: width, height: height);
                Equal(result, expected, item["name"].GetValue<string>());
                ulong used = result.Statistics.WorkUsed;
                if (used == 0 || result.Statistics.WorkBudget != 100000000) throw new Exception("finite native work statistics");
                var exact = program.Generate(seed, width: width, height: height, limits: new ExecutionLimits(used, 60000000000));
                Equal(exact, expected, "exact work " + item["name"].GetValue<string>());
                if (exact.Statistics.WorkRemaining != 0 || exact.Statistics.FuelUsed != result.Statistics.FuelUsed) throw new Exception("work boundary or changed fuel");
                foreach (var control in new[] { (new ExecutionLimits(used - 1), 3015u), (new ExecutionLimits(timeoutNanoseconds: 1), 3017u) })
                {
                    try { program.Generate(seed, width: width, height: height, limits: control.Item1); throw new Exception("expected execution limit"); }
                    catch (IpropException failure) { if (failure.Reason != control.Item2 || failure.Stage != 4) throw new Exception("wrong work/deadline reason"); }
                }
                Equal(program.Generate(seed, width: width, height: height), expected, "recovery");
                program.Generate(seed ^ uint.MaxValue, width: width, height: height);
            }
            Equal(result, expected, "retained " + item["name"].GetValue<string>());
            if (result.Statistics.FuelUsed != result.Statistics.FuelBudget - result.Statistics.FuelRemaining ||
                result.Statistics.OutputElementCount != result.Outputs.Aggregate(0UL, (n,s) => n + s.Count)) throw new Exception("typed statistics");
            if (result.Resource(0) != null || result.Resource((uint)result.Resources.Count + 1) != null) throw new Exception("invalid resource ID");
        }
        Console.WriteLine("Production C# typed parts: PASS 23 complete C++/C fixtures and independent result lifetime");
    }
}
