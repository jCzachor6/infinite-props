using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace InfiniteProps
{
    public enum OutputKind : uint { Point, Rect, Circle }
    public enum InputKind : uint { Color, Float, UInt }

    public readonly struct InputValue
    {
        internal readonly InputKind Kind;
        internal readonly float F;
        internal readonly uint U;
        internal readonly Native.Color C;
        private InputValue(InputKind kind, float f, uint u, Native.Color c) { Kind = kind; F = f; U = u; C = c; }
        public static InputValue Float(float value) => new InputValue(InputKind.Float, value, 0, default);
        public static InputValue UInt(uint value) => new InputValue(InputKind.UInt, 0, value, default);
        public static InputValue Int(int value) => UInt(unchecked((uint)value));
        public static InputValue Color(byte r, byte g, byte b, byte a = 255) =>
            new InputValue(InputKind.Color, 0, 0, new Native.Color { R = r, G = g, B = b, A = a });
        public InputKind Type => Kind;
        public float FloatValue => Kind == InputKind.Float ? F : throw new InvalidOperationException();
        public uint UIntValue => Kind == InputKind.UInt ? U : throw new InvalidOperationException();
        public byte[] ColorValue => Kind == InputKind.Color ? new[] { C.R, C.G, C.B, C.A } : throw new InvalidOperationException();
    }

    public sealed class Input
    {
        public uint Index { get; internal set; }
        public string Name { get; internal set; }
        public InputValue DefaultValue { get; internal set; }
    }
    public sealed class Metadata
    {
        public uint Width { get; internal set; }
        public uint Height { get; internal set; }
        public float OriginX { get; internal set; }
        public float OriginY { get; internal set; }
        public ulong Fuel { get; internal set; }
        public byte[] EmissionColor { get; internal set; }
        public float EmissionOffsetX { get; internal set; }
        public float EmissionOffsetY { get; internal set; }
        public float EmissionRadius { get; internal set; }
        public uint ContainerFlags { get; internal set; }
    }
    public sealed class Output
    {
        public uint Index { get; internal set; }
        public uint Owner { get; internal set; }
        public string Name { get; internal set; }
        public OutputKind Kind { get; internal set; }
        public bool IsList { get; internal set; }
        public uint Count { get; internal set; }
        public float[] Values { get; internal set; }
    }
    public sealed class Statistics
    {
        public ulong FuelBudget { get; internal set; }
        public ulong FuelRemaining { get; internal set; }
        public ulong FuelUsed { get; internal set; }
        public ulong ImageByteCount { get; internal set; }
        public ulong OutputElementCount { get; internal set; }
    }
    public sealed partial class GeneratedImage
    {
        public uint Width { get; internal set; }
        public uint Height { get; internal set; }
        public ulong RowStride { get; internal set; }
        public uint PixelFormat { get; internal set; }
        public uint AlphaMode { get; internal set; }
        public uint StorageOrigin { get; internal set; }
        public byte[] Rgba { get; internal set; }
        public IReadOnlyList<Output> Outputs { get; internal set; }
        public Statistics Statistics { get; internal set; }
        public Output FindOutput(string name, uint owner = 0)
        {
            foreach (var output in Outputs) if (output.Owner == owner && output.Name == name) return output;
            return null;
        }
    }

    public sealed class IpropException : Exception
    {
        public uint Status { get; }
        public uint Reason { get; }
        public uint Stage { get; }
        public ulong ByteOffset { get; }
        public uint LocationFlags { get; }
        public uint OffsetSpace { get; }
        public uint FunctionIndex { get; }
        public uint PC { get; }
        public uint DebugLine { get; }
        public bool MessageTruncated { get; }
        internal IpropException(uint status, Native.Error e) : base(Native.ReadCString(e.Message))
        {
            Status = status; Reason = e.Reason; Stage = e.Stage; ByteOffset = e.ByteOffset;
            LocationFlags = e.LocationFlags; OffsetSpace = e.OffsetSpace;
            FunctionIndex = e.FunctionIndex; PC = e.PC; DebugLine = e.DebugLine;
            MessageTruncated = e.MessageTruncated != 0;
        }
    }

    /// <summary>Calls and disposal serialize per program. Returned arrays own
    /// their data; independent programs may generate concurrently.</summary>
    public sealed class Program : IDisposable
    {
        private readonly object gate = new object();
        private readonly Native.ProgramHandle handle;
        public Metadata Metadata { get; }
        public IReadOnlyList<Input> Inputs { get; }
        public Program(byte[] bytes)
        {
            if (bytes == null) throw new ArgumentNullException(nameof(bytes));
            using (var error = new Native.ErrorBuffer())
            {
                var library = Native.Sized<Native.Library>();
                error.Check(Native.iprop_library_query_ex(ref library, ref error.Value));
                if (library.ApiMajor != 0 || library.ApiMinor < 4 || library.AbiMajor != 0 || library.AbiMinor != 5)
                    throw new NotSupportedException("Infinite Props requires C API 0.4 / ABI 0.5");
                IntPtr pointer;
                error.Check(Native.iprop_program_load_ex(bytes.Length == 0 ? new byte[1] : bytes,
                    (ulong)bytes.LongLength, out pointer, ref error.Value));
                handle = new Native.ProgramHandle(pointer);
                try
                {
                    var meta = Native.Sized<Native.Meta>();
                    error.Check(Native.iprop_program_query(handle, ref meta, ref error.Value));
                    Metadata = new Metadata { Width = meta.Width, Height = meta.Height,
                        OriginX = meta.OriginX, OriginY = meta.OriginY, Fuel = meta.Fuel,
                        EmissionColor = meta.HasEmission == 0 ? null : new[] { meta.EmissionColor.R, meta.EmissionColor.G, meta.EmissionColor.B, meta.EmissionColor.A },
                        EmissionOffsetX = meta.EmissionX, EmissionOffsetY = meta.EmissionY,
                        EmissionRadius = meta.EmissionRadius, ContainerFlags = meta.ContainerFlags };
                    uint count;
                    error.Check(Native.iprop_program_input_count(handle, out count, ref error.Value));
                    var inputs = new List<Input>();
                    for (uint i = 0; i < count; ++i)
                    {
                        var input = Native.Sized<Native.InputInfo>();
                        error.Check(Native.iprop_program_input_get(handle, i, ref input, ref error.Value));
                        var c = input.Color;
                        inputs.Add(new Input { Index = input.Index, Name = Native.ReadString(input.Name),
                            DefaultValue = input.Type == 0 ? InputValue.Color(c.R, c.G, c.B, c.A) :
                                input.Type == 1 ? InputValue.Float(input.F) : InputValue.UInt(input.U) });
                    }
                    Inputs = inputs.AsReadOnly();
                }
                catch { handle.Dispose(); throw; }
            }
        }

        public GeneratedImage Generate(uint seed = 0, IReadOnlyDictionary<string, InputValue> inputs = null,
                                       uint width = 0, uint height = 0)
        {
            lock (gate)
            {
                if (handle.IsClosed) throw new ObjectDisposedException(nameof(Program));
                using (var error = new Native.ErrorBuffer())
                {
                    IntPtr parameters;
                    error.Check(Native.iprop_parameters_create(handle, out parameters, ref error.Value));
                    try
                    {
                        if (inputs != null) foreach (var input in inputs)
                        {
                            var encoded = Native.Utf8.GetBytes(input.Key);
                            var pinned = GCHandle.Alloc(encoded, GCHandleType.Pinned);
                            try
                            {
                                var name = new Native.Utf8View { Data = pinned.AddrOfPinnedObject(), Length = checked((uint)encoded.Length) };
                                var v = input.Value;
                                uint status = v.Kind == InputKind.Color ? Native.iprop_parameters_set_color_named(parameters, name, v.C, ref error.Value) :
                                    v.Kind == InputKind.Float ? Native.iprop_parameters_set_f32_named(parameters, name, v.F, ref error.Value) :
                                    Native.iprop_parameters_set_u32_named(parameters, name, v.U, ref error.Value);
                                error.Check(status);
                            }
                            finally { pinned.Free(); }
                        }
                        var options = Native.Sized<Native.Options>();
                        options.Seed = seed; options.Width = width; options.Height = height;
                        IntPtr result;
                        error.Check(Native.iprop_generate_ex(handle, ref options, parameters, out result, ref error.Value));
                        try { return ReadResult(result, error); }
                        finally { Native.iprop_result_free(result); }
                    }
                    finally { Native.iprop_parameters_free(parameters); }
                }
            }
        }

        private static GeneratedImage ReadResult(IntPtr result, Native.ErrorBuffer error)
        {
            var image = Native.Sized<Native.Image>();
            error.Check(Native.iprop_result_query_image(result, ref image, ref error.Value));
            var rgba = new byte[checked((int)image.ByteCount)];
            Marshal.Copy(image.Pixels, rgba, 0, rgba.Length);
            var stats = Native.Sized<Native.Stats>();
            error.Check(Native.iprop_result_query_stats(result, ref stats, ref error.Value));
            uint count;
            error.Check(Native.iprop_result_output_count(result, out count, ref error.Value));
            var outputs = new List<Output>();
            for (uint i = 0; i < count; ++i)
            {
                var slot = Native.Sized<Native.OutputView>();
                error.Check(Native.iprop_result_output_get(result, i, ref slot, ref error.Value));
                var values = new float[checked((int)((ulong)slot.Count * slot.Stride))];
                if (values.Length != 0) Marshal.Copy(slot.Values, values, 0, values.Length);
                outputs.Add(new Output { Index = slot.Index, Owner = slot.Owner,
                    Name = Native.ReadString(slot.Name), Kind = (OutputKind)slot.Kind,
                    IsList = slot.IsList != 0, Count = slot.Count, Values = values });
            }
            return new GeneratedImage { Width = image.Width, Height = image.Height, RowStride = image.RowStride,
                PixelFormat = image.PixelFormat, AlphaMode = image.AlphaMode, StorageOrigin = image.Origin,
                Rgba = rgba, Outputs = outputs.AsReadOnly(), Statistics = new Statistics {
                    FuelBudget = stats.FuelBudget, FuelRemaining = stats.FuelRemaining, FuelUsed = stats.FuelUsed,
                    ImageByteCount = stats.ImageByteCount, OutputElementCount = stats.OutputElementCount } };
        }
        public void Dispose() { lock (gate) handle.Dispose(); }
    }

    internal static class Native
    {
#if UNITY_IOS && !UNITY_EDITOR
        private const string LibraryName = "__Internal";
#elif UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN || IPROP_WINDOWS
        private const string LibraryName = "ipropc-0.5";
#else
        private const string LibraryName = "ipropc";
#endif
        internal static readonly Encoding Utf8 = new UTF8Encoding(false, true);
        // Every descriptor has a leading size word; default(T) also clears flags.
        internal static T Sized<T>() where T : struct
        {
            var memory = Marshal.AllocHGlobal(Marshal.SizeOf<T>());
            try { Marshal.StructureToPtr(default(T), memory, false); Marshal.WriteInt32(memory, Marshal.SizeOf<T>()); return Marshal.PtrToStructure<T>(memory); }
            finally { Marshal.FreeHGlobal(memory); }
        }
        internal static string ReadString(Utf8View view)
        {
            var bytes = new byte[checked((int)view.Length)];
            if (bytes.Length != 0) Marshal.Copy(view.Data, bytes, 0, bytes.Length);
            return Utf8.GetString(bytes);
        }
        internal static string ReadCString(IntPtr p)
        {
            int count = 0; while (Marshal.ReadByte(p, count) != 0) ++count;
            return ReadString(new Utf8View { Data = p, Length = (uint)count });
        }
        internal sealed class ErrorBuffer : IDisposable
        {
            internal Error Value = Sized<Error>();
            internal ErrorBuffer() { Value.Message = Marshal.AllocHGlobal(8192); Value.MessageCapacity = 8192; Marshal.WriteByte(Value.Message, 0); }
            internal void Check(uint status) { if (status != 0) throw new IpropException(status, Value); }
            public void Dispose() { Marshal.FreeHGlobal(Value.Message); }
        }
        internal sealed class ProgramHandle : SafeHandleZeroOrMinusOneIsInvalid
        {
            internal ProgramHandle(IntPtr value) : base(true) { SetHandle(value); }
            protected override bool ReleaseHandle() { iprop_program_free(handle); return true; }
        }
        [StructLayout(LayoutKind.Sequential)] internal struct Color { internal byte R, G, B, A; }
        [StructLayout(LayoutKind.Sequential)] internal struct Utf8View { internal IntPtr Data; internal uint Length; }
        [StructLayout(LayoutKind.Sequential)] internal struct Library { internal uint Size, ProductMajor, ProductMinor, ProductPatch, ApiMajor, ApiMinor, AbiMajor, AbiMinor, FormatMajor, FormatMinorMin, FormatMinorMax; internal ulong Capabilities; }
        [StructLayout(LayoutKind.Sequential)] internal struct Options { internal uint Size, Flags, Seed, Width, Height; }
        [StructLayout(LayoutKind.Sequential)] internal struct Error { internal uint Size, Status, Reason, Stage; internal ulong ByteOffset; internal uint LocationFlags, OffsetSpace, FunctionIndex, PC, DebugLine; internal IntPtr Message; internal uint MessageCapacity; internal ulong MessageRequired; internal uint MessageTruncated; }
        [StructLayout(LayoutKind.Sequential)] internal struct Meta { internal uint Size, Width, Height; internal float OriginX, OriginY; internal ulong Fuel; internal uint HasEmission; internal Color EmissionColor; internal float EmissionX, EmissionY, EmissionRadius; internal uint FormatMajor, FormatMinor, ContainerFlags; }
        [StructLayout(LayoutKind.Sequential)] internal struct InputInfo { internal uint Size, Flags, Index, Type; internal Utf8View Name; internal Color Color; internal float F; internal uint U; }
        [StructLayout(LayoutKind.Sequential)] internal struct Image { internal uint Size, Flags, Width, Height; internal ulong ByteCount, RowStride; internal uint PixelFormat, AlphaMode, Origin; internal IntPtr Pixels; }
        [StructLayout(LayoutKind.Sequential)] internal struct Stats { internal uint Size, Flags; internal ulong FuelBudget, FuelRemaining, FuelUsed, ImageByteCount, OutputElementCount; }
        [StructLayout(LayoutKind.Sequential)] internal struct OutputView { internal uint Size, Flags, Index, Owner, Kind, IsList, Count, Stride; internal Utf8View Name; internal IntPtr Values; }
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_library_query_ex(ref Library info, ref Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_program_load_ex(byte[] bytes, ulong count, out IntPtr program, ref Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern void iprop_program_free(IntPtr program);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_program_query(ProgramHandle program, ref Meta meta, ref Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_program_input_count(ProgramHandle program, out uint count, ref Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_program_input_get(ProgramHandle program, uint index, ref InputInfo input, ref Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_parameters_create(ProgramHandle program, out IntPtr parameters, ref Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern void iprop_parameters_free(IntPtr parameters);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_parameters_set_f32_named(IntPtr parameters, Utf8View name, float value, ref Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_parameters_set_u32_named(IntPtr parameters, Utf8View name, uint value, ref Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_parameters_set_color_named(IntPtr parameters, Utf8View name, Color value, ref Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_generate_ex(ProgramHandle program, ref Options options, IntPtr parameters, out IntPtr result, ref Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern void iprop_result_free(IntPtr result);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_result_query_image(IntPtr result, ref Image image, ref Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_result_query_stats(IntPtr result, ref Stats stats, ref Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_result_output_count(IntPtr result, out uint count, ref Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_result_output_get(IntPtr result, uint index, ref OutputView output, ref Error error);
    }
}
