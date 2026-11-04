using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace InfiniteProps
{
    [StructLayout(LayoutKind.Sequential)]
    public readonly struct Point
    {
        public readonly float X, Y;
        public Point(float x, float y) { X = x; Y = y; }
    }
    public sealed class PartSnapshot
    {
        public uint OutputIndex { get; internal set; }
        public uint ElementIndex { get; internal set; }
        public int Id { get; internal set; }
        public int SourceId { get; internal set; }
        public uint ShapeKind { get; internal set; }
        public uint AppearanceKind { get; internal set; }
        public uint ShapeResource { get; internal set; }
        public uint RegionIndex { get; internal set; }
        public uint TextureResource { get; internal set; }
        public byte[] Tint { get; internal set; }
        public uint D4 { get; internal set; }
        public int TranslationX { get; internal set; }
        public int TranslationY { get; internal set; }
        public Point SourceScale { get; internal set; }
        public Point TargetScale { get; internal set; }
        public Point SourceAnchor { get; internal set; }
        public Point Center { get; internal set; }
        public Point BoundsOrigin { get; internal set; }
        public Point BoundsSize { get; internal set; }
        public uint SourceD4 { get; internal set; }
        public int SourceOriginX { get; internal set; }
        public int SourceOriginY { get; internal set; }
        public uint TextureD4 { get; internal set; }
        public int TextureOriginX { get; internal set; }
        public int TextureOriginY { get; internal set; }
    }
    public sealed class ResourceRing
    {
        public uint Index { get; internal set; }
        public uint ParentRing { get; internal set; }
        public bool IsHole { get; internal set; }
        public IReadOnlyList<Point> Vertices { get; internal set; }
    }
    public sealed class ResourceRegion
    {
        public uint Index { get; internal set; }
        public uint OuterRing { get; internal set; }
        public IReadOnlyList<uint> Holes { get; internal set; }
        public int SourceId { get; internal set; }
        public float Area { get; internal set; }
        public Point Center { get; internal set; }
        public Point BoundsOrigin { get; internal set; }
        public Point BoundsSize { get; internal set; }
    }
    /// <summary>One owned resource per result-local ID, shared by snapshots.
    /// Data is exact A8/RGBA; geometry retains rings and hole assignments.</summary>
    public sealed class ResultResource
    {
        public uint Id { get; internal set; }
        public uint Kind { get; internal set; }
        public uint PixelFormat { get; internal set; }
        public uint Width { get; internal set; }
        public uint Height { get; internal set; }
        public ulong RowStride { get; internal set; }
        public byte[] Data { get; internal set; }
        public ulong ByteCount => (ulong)Data.LongLength;
        public Point Scale { get; internal set; }
        public uint VertexCount { get; internal set; }
        public uint EdgeCount { get; internal set; }
        public IReadOnlyList<ResourceRing> Rings { get; internal set; }
        public IReadOnlyList<ResourceRegion> Regions { get; internal set; }
    }
    public sealed partial class GeneratedImage
    {
        public IReadOnlyList<ResultResource> Resources { get; internal set; }
        public ResultResource Resource(uint id) => id > 0 && id <= Resources.Count ? Resources[(int)id - 1] : null;
    }
    public sealed partial class Program
    {
        private static IReadOnlyList<PartSnapshot> ReadParts(IntPtr result, uint slot, uint count, Native.ErrorBuffer error)
        {
            var parts = new List<PartSnapshot>();
            for (uint i = 0; i < count; ++i)
            {
                var p = Native.Sized<Native.PartSnapshot>();
                error.Check(Native.iprop_result_output_part(result, slot, i, ref p, ref error.Value));
                parts.Add(new PartSnapshot { OutputIndex = p.OutputIndex, ElementIndex = p.ElementIndex, Id = p.Id, SourceId = p.SourceId,
                    ShapeKind = p.ShapeKind, AppearanceKind = p.AppearanceKind, ShapeResource = p.ShapeResource,
                    RegionIndex = p.RegionIndex, TextureResource = p.TextureResource, Tint = new[] { p.Tint.R, p.Tint.G, p.Tint.B, p.Tint.A },
                    D4 = p.D4, TranslationX = p.TranslationX, TranslationY = p.TranslationY,
                    SourceScale = p.SourceScale, TargetScale = p.TargetScale, SourceAnchor = p.SourceAnchor,
                    Center = p.Center, BoundsOrigin = p.BoundsOrigin, BoundsSize = p.BoundsSize,
                    SourceD4 = p.SourceD4, SourceOriginX = p.SourceOriginX, SourceOriginY = p.SourceOriginY,
                    TextureD4 = p.TextureD4, TextureOriginX = p.TextureOriginX, TextureOriginY = p.TextureOriginY });
            }
            return parts.AsReadOnly();
        }
        private static IReadOnlyList<ResultResource> ReadResources(IntPtr result, Native.ErrorBuffer error)
        {
            uint count;
            error.Check(Native.iprop_result_resource_count(result, out count, ref error.Value));
            var resources = new List<ResultResource>();
            for (uint id = 1; id <= count; ++id)
            {
                var r = Native.Sized<Native.ResourceInfo>();
                error.Check(Native.iprop_result_resource_get(result, id, ref r, ref error.Value));
                var data = new byte[checked((int)r.ByteCount)];
                if (data.Length != 0) Marshal.Copy(r.Data, data, 0, data.Length);
                var rings = new List<ResourceRing>();
                for (uint i = 0; i < r.RingCount; ++i)
                {
                    var v = Native.Sized<Native.RingInfo>();
                    error.Check(Native.iprop_result_resource_ring(result, id, i, ref v, ref error.Value));
                    var raw = new float[checked((int)v.VertexCount * 2)];
                    if (raw.Length != 0) error.Check(Native.iprop_result_resource_ring_copy_vertices(result, id, i, raw, (ulong)raw.LongLength * 4, ref error.Value));
                    var vertices = new List<Point>();
                    for (int k = 0; k < raw.Length; k += 2) vertices.Add(new Point(raw[k], raw[k + 1]));
                    rings.Add(new ResourceRing { Index = v.Index, ParentRing = v.ParentRing, IsHole = v.IsHole != 0, Vertices = vertices.AsReadOnly() });
                }
                var regions = new List<ResourceRegion>();
                for (uint i = 0; i < r.RegionCount; ++i)
                {
                    var v = Native.Sized<Native.RegionInfo>();
                    error.Check(Native.iprop_result_resource_region(result, id, i, ref v, ref error.Value));
                    var holes = new List<uint>();
                    for (uint h = 0; h < v.HoleCount; ++h) {
                        uint ring; error.Check(Native.iprop_result_resource_region_hole(result, id, i, h, out ring, ref error.Value)); holes.Add(ring);
                    }
                    regions.Add(new ResourceRegion { Index = v.Index, OuterRing = v.OuterRing, Holes = holes.AsReadOnly(),
                        SourceId = v.SourceId, Area = v.Area, Center = v.Center, BoundsOrigin = v.BoundsOrigin, BoundsSize = v.BoundsSize });
                }
                resources.Add(new ResultResource { Id = r.Id, Kind = r.Kind, PixelFormat = r.PixelFormat, Width = r.Width, Height = r.Height,
                    RowStride = r.RowStride, Data = data, Scale = r.Scale, VertexCount = r.VertexCount, EdgeCount = r.EdgeCount,
                    Rings = rings.AsReadOnly(), Regions = regions.AsReadOnly() });
            }
            return resources.AsReadOnly();
        }
    }
    internal static partial class Native
    {
        [StructLayout(LayoutKind.Sequential)] internal struct OutputSlot { internal uint Size, Flags, Index, Owner, Kind, IsList, Count; internal Utf8View Name; }
        [StructLayout(LayoutKind.Sequential)] internal struct PartSnapshot {
            internal uint Size, Flags, OutputIndex, ElementIndex; internal int Id, SourceId;
            internal uint ShapeKind, AppearanceKind, ShapeResource, RegionIndex, TextureResource;
            internal Color Tint; internal uint D4; internal int TranslationX, TranslationY;
            internal Point SourceScale, TargetScale, SourceAnchor, Center, BoundsOrigin, BoundsSize;
            internal uint SourceD4; internal int SourceOriginX, SourceOriginY;
            internal uint TextureD4; internal int TextureOriginX, TextureOriginY;
        }
        [StructLayout(LayoutKind.Sequential)] internal struct ResourceInfo {
            internal uint Size, Flags, Id, Kind, PixelFormat, Width, Height;
            internal ulong RowStride, ByteCount; internal IntPtr Data; internal Point Scale;
            internal uint VertexCount, EdgeCount, RingCount, RegionCount;
        }
        [StructLayout(LayoutKind.Sequential)] internal struct RingInfo { internal uint Size, Flags, Index, VertexCount, ParentRing, IsHole; }
        [StructLayout(LayoutKind.Sequential)] internal struct RegionInfo {
            internal uint Size, Flags, Index, OuterRing, HoleCount; internal int SourceId; internal float Area; internal Point Center, BoundsOrigin, BoundsSize;
        }
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_result_output_slot(IntPtr result, uint index, ref OutputSlot slot, ref Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_result_output_part(IntPtr result, uint slot, uint index, ref PartSnapshot part, ref Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_result_resource_count(IntPtr result, out uint count, ref Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_result_resource_get(IntPtr result, uint id, ref ResourceInfo resource, ref Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_result_resource_ring(IntPtr result, uint id, uint index, ref RingInfo ring, ref Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_result_resource_ring_copy_vertices(IntPtr result, uint id, uint index, [Out] float[] vertices, ulong capacity, ref Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_result_resource_region(IntPtr result, uint id, uint index, ref RegionInfo region, ref Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_result_resource_region_hole(IntPtr result, uint id, uint index, uint hole, out uint ring, ref Error error);
    }
}
