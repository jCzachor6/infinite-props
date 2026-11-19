using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using Microsoft.Win32.SafeHandles;

namespace InfiniteProps
{
    internal static class Native3D
    {
#if UNITY_IOS && !UNITY_EDITOR
        private const string LibraryName = "__Internal";
#else
        private const string LibraryName = "iprop3dc";
#endif
        [StructLayout(LayoutKind.Sequential)]
        internal struct LibraryInfo3D
        {
            internal uint StructSize;
            internal uint ApiMajor;
            internal uint ApiMinor;
            internal uint MaxVertices;
            internal uint MaxTriangles;
            internal uint MaxTextures;
            internal uint MaxTextureSide;
            internal ulong MaxTexels;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct ModelInfo
        {
            internal uint StructSize;
            internal uint Flags;
            internal float SizeX;
            internal float SizeY;
            internal float SizeZ;
            internal float UnitsPerMeter;
            internal ulong Fuel;
            internal uint InputCount;
            internal uint SkinProgramCount;
            internal uint ContainerFlags;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct SkinProgramInfo
        {
            internal uint StructSize;
            internal uint Flags;
            internal uint ProgramIndex;
            internal uint Width;
            internal uint Height;
            internal Native.Utf8View Name;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct KnownTexture
        {
            internal ulong Key;
            internal ulong Fuel;
            internal ulong Work;
            internal uint ImageKinds;
            internal uint Reserved;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct GenerateOptions
        {
            internal uint StructSize;
            internal uint Flags;
            internal uint Seed;
            internal uint Lod;
            internal IntPtr KnownTextures;
            internal uint KnownTextureCount;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct ResultInfo
        {
            internal uint StructSize;
            internal uint Flags;
            internal float BoundsMinX, BoundsMinY, BoundsMinZ;
            internal float BoundsMaxX, BoundsMaxY, BoundsMaxZ;
            internal float SizeX, SizeY, SizeZ;
            internal float UnitsPerMeter;
            internal uint VertexCount;
            internal uint TriangleCount;
            internal uint SurfaceCount;
            internal uint TextureCount;
            internal ulong FuelBudget;
            internal ulong FuelUsed;
            internal ulong WorkBudget;
            internal ulong WorkUsed;
            internal uint OutputCount;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct SurfaceView
        {
            internal uint StructSize;
            internal uint Flags;
            internal uint SurfaceIndex;
            internal uint TextureIndex;
            internal float Roughness;
            internal float Metallic;
            internal float Emission;
            internal uint AlphaMode;
            internal float AlphaCutoff;
            internal uint TwoSided;
            internal uint Crisp;
            internal uint VertexCount;
            internal uint IndexCount;
            internal IntPtr Positions;
            internal IntPtr Normals;
            internal IntPtr Tangents;
            internal IntPtr Uvs;
            internal IntPtr Indices;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct TextureView
        {
            internal uint StructSize;
            internal uint Flags;
            internal uint TextureIndex;
            internal uint Repeats;
            internal ulong Key;
            internal Native.Utf8View Name;
            internal uint AssetWidth;
            internal uint AssetHeight;
            internal uint Width;
            internal uint Height;
            internal ulong ByteCount;
            internal ulong RowStride;
            internal uint PixelFormat;
            internal uint AlphaMode;
            internal uint Origin;
            internal uint ImageCount;
            internal IntPtr Pixels;
            internal ulong Fuel;
            internal ulong Work;
            internal uint ImageKinds;
            internal uint Reused;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct OutputView
        {
            internal uint StructSize;
            internal uint Flags;
            internal uint OutputIndex;
            internal uint Kind;
            internal uint List;
            internal uint ValueCount;
            internal uint FloatsPerValue;
            internal Native.Utf8View Name;
            internal IntPtr Values;
        }
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint iprop3d_library_query(ref LibraryInfo3D info, ref Native.Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ulong iprop3d_texture_stamp();
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint iprop3d_model_load(byte[] bytes, ulong count, out IntPtr model, ref Native.Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void iprop3d_model_free(IntPtr model);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint iprop3d_model_query(IntPtr model, ref ModelInfo info, ref Native.Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint iprop3d_model_skin_program(IntPtr model, uint index, ref SkinProgramInfo info, ref Native.Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint iprop3d_model_input_get(IntPtr model, uint index, ref Native.InputInfo info, ref Native.Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint iprop3d_model_parameters_create(IntPtr model, out IntPtr parameters, ref Native.Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint iprop3d_generate(IntPtr model, ref GenerateOptions options, IntPtr parameters, ref Native.Limits limits, out IntPtr result, ref Native.Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void iprop3d_result_free(IntPtr result);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint iprop3d_result_query(IntPtr result, ref ResultInfo info, ref Native.Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint iprop3d_result_surface(IntPtr result, uint index, ref SurfaceView surface, ref Native.Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint iprop3d_result_texture(IntPtr result, uint index, ref TextureView texture, ref Native.Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint iprop3d_result_texture_image(IntPtr result, uint texture, uint index, ref Native.AuxImage view, ref Native.Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint iprop3d_result_output(IntPtr result, uint index, ref OutputView output, ref Native.Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint iprop_cancellation_create(out IntPtr token, ref Native.Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void iprop_cancellation_cancel(IntPtr token);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void iprop_cancellation_free(IntPtr token);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint iprop_library_query_ex(ref Native.Library info, ref Native.Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void iprop_parameters_free(IntPtr parameters);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint iprop_parameters_set_f32_named(IntPtr parameters, Native.Utf8View name, float value, ref Native.Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint iprop_parameters_set_u32_named(IntPtr parameters, Native.Utf8View name, uint value, ref Native.Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint iprop_parameters_set_color_named(IntPtr parameters, Native.Utf8View name, Native.Color value, ref Native.Error error);
    }

    public sealed partial class ModelLibrary
    {
        public uint ApiMajor { get; internal set; }
        public uint ApiMinor { get; internal set; }
        public uint MaxVertices { get; internal set; }
        public uint MaxTriangles { get; internal set; }
        public uint MaxTextures { get; internal set; }
        public uint MaxTextureSide { get; internal set; }
        public uint MaxLod { get; internal set; }
        public ulong MaxTexels { get; internal set; }
        public ulong TextureStamp { get; internal set; }
    }

    public sealed partial class ModelMetadata
    {
        public float SizeX { get; internal set; }
        public float SizeY { get; internal set; }
        public float SizeZ { get; internal set; }
        public float UnitsPerMeter { get; internal set; }
        public ulong Fuel { get; internal set; }
        public uint ContainerFlags { get; internal set; }
    }

    public sealed partial class SkinProgram
    {
        public uint Index { get; internal set; }
        public uint Width { get; internal set; }
        public uint Height { get; internal set; }
        public string Name { get; internal set; }
    }

    public sealed partial class ModelStatistics
    {
        public uint VertexCount { get; internal set; }
        public uint TriangleCount { get; internal set; }
        public ulong FuelBudget { get; internal set; }
        public ulong FuelUsed { get; internal set; }
        public ulong WorkBudget { get; internal set; }
        public ulong WorkUsed { get; internal set; }
    }

    public sealed partial class Surface
    {
        public uint Index { get; internal set; }
        public uint TextureIndex { get; internal set; }
        public uint AlphaMode { get; internal set; }
        public float Roughness { get; internal set; }
        public float Metallic { get; internal set; }
        public float Emission { get; internal set; }
        public float AlphaCutoff { get; internal set; }
        public bool TwoSided { get; internal set; }
        public bool Crisp { get; internal set; }
        public float[] Positions { get; internal set; }
        public float[] Normals { get; internal set; }
        public float[] Tangents { get; internal set; }
        public float[] Uvs { get; internal set; }
        public int[] Indices { get; internal set; }
    }

    public sealed partial class ModelTexture
    {
        public uint Index { get; internal set; }
        public uint Width { get; internal set; }
        public uint Height { get; internal set; }
        public uint AssetWidth { get; internal set; }
        public uint AssetHeight { get; internal set; }
        public uint PixelFormat { get; internal set; }
        public uint AlphaMode { get; internal set; }
        public uint Origin { get; internal set; }
        public ulong RowStride { get; internal set; }
        public KnownTexture Known { get; internal set; }
        public bool Repeats { get; internal set; }
        public bool Reused { get; internal set; }
        public byte[] Rgba { get; internal set; }
        public AuxiliaryImage[] Images { get; internal set; }
        public string Name { get; internal set; }
    }

    public sealed partial class ModelOutput
    {
        public uint Index { get; internal set; }
        public uint Kind { get; internal set; }
        public uint ValueCount { get; internal set; }
        public uint FloatsPerValue { get; internal set; }
        public string Name { get; internal set; }
        public bool IsList { get; internal set; }
        public float[] Values { get; internal set; }
    }

    public sealed partial class GeneratedModel
    {
        public float[] Size { get; internal set; }
        public float[] BoundsMin { get; internal set; }
        public float[] BoundsMax { get; internal set; }
        public float UnitsPerMeter { get; internal set; }
        public ModelStatistics Statistics { get; internal set; }
        public Surface[] Surfaces { get; internal set; }
        public ModelTexture[] Textures { get; internal set; }
        public ModelOutput[] Outputs { get; internal set; }
    }

    public readonly struct KnownTexture
    {
        public readonly ulong Key, Fuel, Work;
        public readonly uint ImageKinds;
        public string KeyHex => Key.ToString("x16");
        public KnownTexture(ulong key, ulong fuel, ulong work, uint imageKinds)
        { Key = key; Fuel = fuel; Work = work; ImageKinds = imageKinds; }
    }

    public sealed partial class ModelTexture
    {
        public ulong Key => Known.Key;
    }

    public sealed class TextureCache
    {
        private readonly Dictionary<ulong, ModelTexture> entries = new Dictionary<ulong, ModelTexture>();
        internal Dictionary<ulong, ModelTexture> Snapshot()
        { lock (entries) return new Dictionary<ulong, ModelTexture>(entries); }
        public IReadOnlyList<KnownTexture> Known => Snapshot().Values.Select(t => t.Known).ToArray();
        public ModelTexture Find(ulong key) { lock (entries) return entries.TryGetValue(key, out var t) ? t : null; }
        public int Count { get { lock (entries) return entries.Count; } }
        public void Clear() { lock (entries) entries.Clear(); }
        public void Remove(ulong key) { lock (entries) entries.Remove(key); }
        internal void Store(ModelTexture[] textures)
        { lock (entries) foreach (var t in textures) if (!t.Reused) entries[t.Key] = t; }
    }

    public sealed class Model : IDisposable
    {
        private sealed class Handle : SafeHandleZeroOrMinusOneIsInvalid
        {
            internal Handle(IntPtr value) : base(true) { SetHandle(value); }
            protected override bool ReleaseHandle() { Native3D.iprop3d_model_free(handle); return true; }
        }
        private readonly Handle handle;
        private readonly object gate = new object();
        private bool disposed;
        public ModelMetadata Metadata { get; }
        public IReadOnlyList<Input> Inputs { get; }
        public IReadOnlyList<SkinProgram> SkinPrograms { get; }
        public static ModelLibrary Library
        {
            get
            {
                try
                {
                    using (var e = new Native.ErrorBuffer())
                    {
                        var info = Native.Sized<Native3D.LibraryInfo3D>();
                        e.Check(Native3D.iprop3d_library_query(ref info, ref e.Value));
                        var flat = Native.Sized<Native.Library>();
                        e.Check(Native3D.iprop_library_query_ex(ref flat, ref e.Value));
                        if (info.ApiMajor != 0 || info.ApiMinor != 3 || flat.ApiMajor != 0 || flat.ApiMinor < 7 ||
                            flat.AbiMajor != 0 || flat.AbiMinor != 5 || (flat.Capabilities & (31UL << 18)) != (31UL << 18))
                            throw new NotSupportedException("Incompatible Infinite Props model library");
                        return new ModelLibrary { ApiMajor = info.ApiMajor, ApiMinor = info.ApiMinor, MaxVertices = info.MaxVertices,
                            MaxTriangles = info.MaxTriangles, MaxTextures = info.MaxTextures, MaxTextureSide = info.MaxTextureSide,
                            MaxTexels = info.MaxTexels, MaxLod = 7, TextureStamp = Native3D.iprop3d_texture_stamp() };
                    }
                }
                catch (Exception ex) when (ex is DllNotFoundException || ex is EntryPointNotFoundException)
                { throw new NotSupportedException("Build the Unity native plugin with package-native.py --models", ex); }
            }
        }

        public Model(byte[] bytes)
        {
            if (bytes == null) throw new ArgumentNullException(nameof(bytes));
            _ = Library;
            using (var e = new Native.ErrorBuffer())
            {
                e.Check(Native3D.iprop3d_model_load(bytes.Length == 0 ? new byte[1] : bytes, (ulong)bytes.Length, out var raw, ref e.Value));
                handle = new Handle(raw);
                try
                {
                    var m = Native.Sized<Native3D.ModelInfo>();
                    e.Check(Native3D.iprop3d_model_query(raw, ref m, ref e.Value));
                    Metadata = new ModelMetadata { SizeX = m.SizeX, SizeY = m.SizeY, SizeZ = m.SizeZ,
                        UnitsPerMeter = m.UnitsPerMeter, Fuel = m.Fuel, ContainerFlags = m.ContainerFlags };
                    var inputs = new List<Input>();
                    for (uint i = 0; i < m.InputCount; ++i)
                    {
                        var v = Native.Sized<Native.InputInfo>();
                        e.Check(Native3D.iprop3d_model_input_get(raw, i, ref v, ref e.Value));
                        var c = v.Color;
                        inputs.Add(new Input { Index = i, Name = Native.ReadString(v.Name), DefaultValue = v.Type == 0
                            ? InputValue.Color(c.R,c.G,c.B,c.A) : v.Type == 1 ? InputValue.Float(v.F) : InputValue.UInt(v.U) });
                    }
                    Inputs = inputs.AsReadOnly();
                    var skins = new List<SkinProgram>();
                    for (uint i = 0; i < m.SkinProgramCount; ++i)
                    {
                        var v = Native.Sized<Native3D.SkinProgramInfo>();
                        e.Check(Native3D.iprop3d_model_skin_program(raw, i, ref v, ref e.Value));
                        skins.Add(new SkinProgram { Index = i, Name = Native.ReadString(v.Name), Width = v.Width, Height = v.Height });
                    }
                    SkinPrograms = skins.AsReadOnly();
                }
                catch { handle.Dispose(); throw; }
            }
        }
        public void Dispose()
        {
            lock (gate)
            {
                disposed = true;
                handle.Dispose();
            }
        }

        public GeneratedModel Generate(uint seed = 0, IReadOnlyDictionary<string, InputValue> inputs = null,
            uint lod = 0, TextureCache textures = null, IReadOnlyList<KnownTexture> known = null, ExecutionLimits limits = null)
        {
            bool retained = false;
            try
            {
                lock (gate)
                {
                    if (disposed) throw new ObjectDisposedException(nameof(Model));
                    handle.DangerousAddRef(ref retained);
                }
                var model = handle.DangerousGetHandle();
                using (var e = new Native.ErrorBuffer())
                {
                    e.Check(Native3D.iprop3d_model_parameters_create(model, out var parameters, ref e.Value));
                    try
                    {
                        if (inputs != null) foreach (var pair in inputs)
                        {
                            var bytes = Native.Utf8.GetBytes(pair.Key);
                            var pin = GCHandle.Alloc(bytes, GCHandleType.Pinned);
                            try
                            {
                                var name = new Native.Utf8View { Data = pin.AddrOfPinnedObject(), Length = (uint)bytes.Length };
                                var v = pair.Value;
                                e.Check(v.Kind == InputKind.Color ? Native3D.iprop_parameters_set_color_named(parameters, name, v.C, ref e.Value)
                                    : v.Kind == InputKind.Float ? Native3D.iprop_parameters_set_f32_named(parameters, name, v.F, ref e.Value)
                                    : Native3D.iprop_parameters_set_u32_named(parameters, name, v.U, ref e.Value));
                            }
                            finally { pin.Free(); }
                        }
                        var snapshot = textures?.Snapshot();
                        var records = (snapshot?.Values.Select(t => t.Known) ?? Enumerable.Empty<KnownTexture>())
                            .Concat(known ?? Array.Empty<KnownTexture>()).Select(t => new Native3D.KnownTexture {
                                Key = t.Key, Fuel = t.Fuel, Work = t.Work, ImageKinds = t.ImageKinds }).ToArray();
                        var options = Native.Sized<Native3D.GenerateOptions>();
                        options.Seed = seed; options.Lod = lod;
                        var control = Native.Sized<Native.Limits>();
                        control.WorkLimit = limits?.WorkLimit ?? 0;
                        control.TimeoutNanoseconds = limits?.TimeoutNanoseconds ?? 0;
                        var pinRecords = GCHandle.Alloc(records, GCHandleType.Pinned);
                        var registration = default(CancellationTokenRegistration);
                        try
                        {
                            options.KnownTextures = pinRecords.AddrOfPinnedObject();
                            options.KnownTextureCount = (uint)records.Length;
                            if (limits != null && limits.Cancellation.CanBeCanceled)
                            {
                                e.Check(Native3D.iprop_cancellation_create(out var token, ref e.Value));
                                control.Cancellation = token;
                                registration = limits.Cancellation.Register(() => Native3D.iprop_cancellation_cancel(token));
                            }
                            e.Check(Native3D.iprop3d_generate(model, ref options, parameters, ref control, out var result, ref e.Value));
                            try
                            {
                                var generated = Copy(result, snapshot, e);
                                textures?.Store(generated.Textures);
                                return generated;
                            }
                            finally { Native3D.iprop3d_result_free(result); }
                        }
                        finally
                        {
                            registration.Dispose();
                            Native3D.iprop_cancellation_free(control.Cancellation);
                            pinRecords.Free();
                        }
                    }
                    finally { Native3D.iprop_parameters_free(parameters); }
                }
            }
            finally { if (retained) handle.DangerousRelease(); }
        }

        private static float[] Floats(IntPtr data, uint count)
        {
            var array = new float[checked((int)count)];
            if (count != 0) Marshal.Copy(data, array, 0, array.Length);
            return array;
        }
        private static byte[] Bytes(IntPtr data, ulong count)
        {
            var array = new byte[checked((int)count)];
            if (count != 0) Marshal.Copy(data, array, 0, array.Length);
            return array;
        }

        private static GeneratedModel Copy(IntPtr result, Dictionary<ulong, ModelTexture> snapshot, Native.ErrorBuffer e)
        {
            var r = Native.Sized<Native3D.ResultInfo>();
            e.Check(Native3D.iprop3d_result_query(result, ref r, ref e.Value));
            var m = new GeneratedModel { Size = new[] { r.SizeX, r.SizeY, r.SizeZ }, UnitsPerMeter = r.UnitsPerMeter,
                BoundsMin = new[] { r.BoundsMinX, r.BoundsMinY, r.BoundsMinZ }, BoundsMax = new[] { r.BoundsMaxX, r.BoundsMaxY, r.BoundsMaxZ },
                Statistics = new ModelStatistics { VertexCount = r.VertexCount, TriangleCount = r.TriangleCount, FuelBudget = r.FuelBudget,
                    FuelUsed = r.FuelUsed, WorkBudget = r.WorkBudget, WorkUsed = r.WorkUsed },
                Surfaces = new Surface[r.SurfaceCount], Textures = new ModelTexture[r.TextureCount], Outputs = new ModelOutput[r.OutputCount] };
            for (uint i = 0; i < r.SurfaceCount; ++i)
            {
                var s = Native.Sized<Native3D.SurfaceView>();
                e.Check(Native3D.iprop3d_result_surface(result, i, ref s, ref e.Value));
                var indices = new int[s.IndexCount];
                if (indices.Length != 0) Marshal.Copy(s.Indices, indices, 0, indices.Length);
                m.Surfaces[i] = new Surface { Index = i, TextureIndex = s.TextureIndex, Roughness = s.Roughness, Metallic = s.Metallic,
                    Emission = s.Emission, AlphaMode = s.AlphaMode, AlphaCutoff = s.AlphaCutoff, TwoSided = s.TwoSided != 0,
                    Crisp = s.Crisp != 0, Positions = Floats(s.Positions, 3*s.VertexCount), Normals = Floats(s.Normals, 3*s.VertexCount),
                    Tangents = Floats(s.Tangents, 4*s.VertexCount), Uvs = Floats(s.Uvs, 2*s.VertexCount), Indices = indices };
            }
            for (uint i = 0; i < r.TextureCount; ++i)
            {
                var t = Native.Sized<Native3D.TextureView>();
                e.Check(Native3D.iprop3d_result_texture(result, i, ref t, ref e.Value));
                var stored = t.Reused != 0 && snapshot != null ? snapshot[t.Key] : null;
                var images = stored?.Images ?? new AuxiliaryImage[t.ImageCount];
                for (uint j = 0; j < t.ImageCount; ++j)
                {
                    var a = Native.Sized<Native.AuxImage>();
                    e.Check(Native3D.iprop3d_result_texture_image(result, i, j, ref a, ref e.Value));
                    images[j] = new AuxiliaryImage { Index = j, Name = Native.ReadString(a.Name), Kind = (ImageKind)a.Kind,
                        Source = (ImageSource)a.Source, Width = a.Width, Height = a.Height, RowStride = a.RowStride,
                        PixelFormat = a.PixelFormat, AlphaMode = a.AlphaMode, StorageOrigin = a.Origin, Rgba = Bytes(a.Pixels, a.ByteCount) };
                }
                m.Textures[i] = new ModelTexture { Index = i, Known = new KnownTexture(t.Key, t.Fuel, t.Work, t.ImageKinds),
                    Name = Native.ReadString(t.Name), Width = t.Width, Height = t.Height, AssetWidth = t.AssetWidth, AssetHeight = t.AssetHeight,
                    Repeats = t.Repeats != 0, Reused = t.Reused != 0, RowStride = stored?.RowStride ?? t.RowStride, PixelFormat = stored?.PixelFormat ?? t.PixelFormat,
                    AlphaMode = stored?.AlphaMode ?? t.AlphaMode, Origin = stored?.Origin ?? t.Origin, Images = images, Rgba = stored?.Rgba ?? Bytes(t.Pixels, t.ByteCount) };
            }
            for (uint i = 0; i < r.OutputCount; ++i)
            {
                var o = Native.Sized<Native3D.OutputView>();
                e.Check(Native3D.iprop3d_result_output(result, i, ref o, ref e.Value));
                m.Outputs[i] = new ModelOutput { Index = i, Kind = o.Kind, Name = Native.ReadString(o.Name), IsList = o.List != 0,
                    ValueCount = o.ValueCount, FloatsPerValue = o.FloatsPerValue, Values = Floats(o.Values, checked(o.ValueCount*o.FloatsPerValue)) };
            }
            return m;
        }
    }
}
