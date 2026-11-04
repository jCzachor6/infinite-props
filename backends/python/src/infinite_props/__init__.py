"""Deterministic native .iprop generation. No game-engine dependency."""
import ctypes as c
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path
import struct
from threading import RLock
import weakref

from . import _native as n
from ._native import IpropError

__all__ = ["Program", "Color", "F32", "U32", "I32", "Input", "Metadata", "Output", "OutputKind", "Statistics", "GeneratedImage", "IpropError",
    "Point", "PartSnapshot", "Resource", "Ring", "Region"]


def _integer(value, minimum, maximum):
    if isinstance(value, bool) or not isinstance(value, int) or not minimum <= value <= maximum:
        raise ValueError(f"Expected an integer in {minimum}..{maximum}")
    return value


@dataclass(frozen=True)
class Color:
    r: int
    g: int
    b: int
    a: int = 255

    def __post_init__(self):
        for channel in (self.r, self.g, self.b, self.a):
            _integer(channel, 0, 255)


@dataclass(frozen=True, init=False)
class F32:
    """An exact binary32 input, including NaN payloads and negative zero."""
    bits: int

    def __init__(self, value):
        object.__setattr__(self, "bits", struct.unpack("=I", struct.pack("=f", value))[0])

    @classmethod
    def from_bits(cls, bits):
        result = cls.__new__(cls)
        object.__setattr__(result, "bits", _integer(bits, 0, 0xffffffff))
        return result

    @property
    def value(self):
        return struct.unpack("=f", struct.pack("=I", self.bits))[0]


@dataclass(frozen=True)
class U32:
    value: int
    def __post_init__(self):
        _integer(self.value, 0, 0xffffffff)


@dataclass(frozen=True)
class I32:
    value: int
    def __post_init__(self):
        _integer(self.value, -0x80000000, 0x7fffffff)


@dataclass(frozen=True)
class Input:
    index: int
    name: str
    default_value: Color | F32 | U32


@dataclass(frozen=True)
class Metadata:
    width: int
    height: int
    origin_x: float
    origin_y: float
    fuel: int
    emission_color: Color | None
    emission_offset_x: float
    emission_offset_y: float
    emission_radius: float
    container_flags: int


class OutputKind(IntEnum):
    POINT = 0
    RECT = 1
    CIRCLE = 2
    PART = 3


@dataclass(frozen=True)
class Point:
    x: float
    y: float


@dataclass(frozen=True)
class PartSnapshot:
    """Exact C API snapshot. Resource IDs address this result's resource table.

    D4 and integer translations are already effective; do not round or apply
    the import transform again. 0xffffffff is the absent raster region index.
    """
    output_index: int
    element_index: int
    id: int
    source_id: int
    shape_kind: int
    appearance_kind: int
    shape_resource: int
    region_index: int
    texture_resource: int
    tint: Color
    d4: int
    translation_x: int
    translation_y: int
    source_scale: Point
    target_scale: Point
    source_anchor: Point
    center: Point
    bounds_origin: Point
    bounds_size: Point
    source_d4: int
    source_origin_x: int
    source_origin_y: int
    texture_d4: int
    texture_origin_x: int
    texture_origin_y: int


@dataclass(frozen=True)
class Ring:
    index: int
    parent_ring: int
    is_hole: bool
    vertices: tuple[Point, ...]


@dataclass(frozen=True)
class Region:
    index: int
    outer_ring: int
    holes: tuple[int, ...]
    source_id: int
    area: float
    center: Point
    bounds_origin: Point
    bounds_size: Point


@dataclass(frozen=True)
class Resource:
    """One owned copy per C resource ID; geometry keeps rings and holes."""
    id: int
    kind: int
    pixel_format: int
    width: int
    height: int
    row_stride: int
    data: bytes
    scale: Point
    vertex_count: int
    edge_count: int
    rings: tuple[Ring, ...]
    regions: tuple[Region, ...]

    @property
    def byte_count(self):
        return len(self.data)


@dataclass(frozen=True)
class Output:
    index: int
    owner: int
    name: str
    kind: OutputKind
    is_list: bool
    count: int
    value_bits: tuple[int, ...]
    parts: tuple[PartSnapshot, ...] = ()

    @property
    def values(self):
        """Convenience Python floats. value_bits preserves the exact f32 leaves."""
        return tuple(struct.unpack("=f", struct.pack("=I", bits))[0] for bits in self.value_bits)


@dataclass(frozen=True)
class Statistics:
    fuel_budget: int
    fuel_remaining: int
    fuel_used: int
    image_byte_count: int
    output_element_count: int


@dataclass(frozen=True)
class GeneratedImage:
    width: int
    height: int
    row_stride: int
    pixel_format: int
    alpha_mode: int
    storage_origin: int
    rgba: bytes
    outputs: tuple[Output, ...]
    statistics: Statistics
    resources: tuple[Resource, ...] = ()

    def resource(self, resource_id):
        """Resolve a result-local nonzero ID; invalid IDs are never indices."""
        _integer(resource_id, 1, len(self.resources))
        return self.resources[resource_id - 1]

    def output(self, name, owner=0):
        return next((slot for slot in self.outputs if slot.owner == owner and slot.name == name), None)

    def to_pillow(self):
        """Interpret stored channels as straight RGBA for presentation.

        rgba remains the exact runtime buffer with legacy destination alpha
        (alpha_mode == 1); already-composited pixels cannot be unblended.
        """
        from PIL import Image
        return Image.frombytes("RGBA", (self.width, self.height), self.rgba)


class Program:
    """Owns a validated program. Calls and close serialize per instance.

    Use independent instances for parallel native generation. Every generation
    starts with fresh defaults and returns independently owned immutable data.
    """
    def __init__(self, source, *, library=None):
        self._lock = RLock()
        self._native = n.Native(library)
        self._handle = c.c_void_p()
        data = bytes(source) if isinstance(source, (bytes, bytearray, memoryview)) else Path(source).read_bytes()
        buffer = c.create_string_buffer(data)
        self._native.call("program_load_ex", buffer, len(data), c.byref(self._handle))
        self._finalizer = weakref.finalize(self, self._native.program_free, self._handle)
        try:
            meta = n.sized(n.Meta)
            self._native.call("program_query", self._handle, c.byref(meta))
            emission = meta.emission_color
            self.metadata = Metadata(meta.size_x, meta.size_y, meta.origin_x, meta.origin_y, meta.fuel,
                Color(emission.r, emission.g, emission.b, emission.a) if meta.has_emission else None,
                meta.emission_offset_x, meta.emission_offset_y, meta.emission_radius, meta.container_flags)
            count = c.c_uint32()
            self._native.call("program_input_count", self._handle, c.byref(count))
            inputs = []
            for index in range(count.value):
                info = n.sized(n.Input)
                self._native.call("program_input_get", self._handle, index, c.byref(info))
                color = info.color_default
                if info.type == 0:
                    value = Color(color.r, color.g, color.b, color.a)
                elif info.type == 1:
                    bits = struct.unpack("=I", c.string_at(c.addressof(info) + n.Input.f32_default.offset, 4))[0]
                    value = F32.from_bits(bits)
                else:
                    value = U32(info.u32_default)
                inputs.append(Input(info.input_index, n.text(info.name), value))
            self.inputs = tuple(inputs)
        except BaseException:
            self.close()
            raise

    def close(self):
        with self._lock:
            self._finalizer()

    def __enter__(self):
        if not self._finalizer.alive:
            raise RuntimeError("Program is closed")
        return self

    def __exit__(self, *exc):
        self.close()

    def generate(self, seed=0, *, inputs=None, width=0, height=0):
        for number in (seed, width, height):
            _integer(number, 0, 0xffffffff)
        with self._lock:
            if not self._finalizer.alive:
                raise RuntimeError("Program is closed")
            parameters = c.c_void_p()
            self._native.call("parameters_create", self._handle, c.byref(parameters))
            try:
                for name, value in (inputs or {}).items():
                    encoded = name.encode("utf-8")
                    _integer(len(encoded), 0, 0xffffffff)
                    buffer = c.create_string_buffer(encoded)
                    view = n.View(c.addressof(buffer), len(encoded))
                    parameter = n.sized(n.Parameter)
                    if isinstance(value, Color):
                        parameter.type = 0
                        parameter.color = n.Color(value.r, value.g, value.b, value.a)
                    elif isinstance(value, F32):
                        parameter.type = 1
                        c.memmove(c.addressof(parameter) + n.Parameter.f32.offset, struct.pack("=I", value.bits), 4)
                    elif isinstance(value, (U32, I32)):
                        parameter.type = 2
                        parameter.u32 = value.value & 0xffffffff
                    else:
                        raise TypeError("Inputs must be Color, F32, U32 or I32")
                    self._native.call("parameters_set_named", parameters, view, c.byref(parameter))
                options = n.sized(n.Options)
                options.flags = 1  # IPROP_GENERATE_PART_OUTPUTS, negotiated by Native.
                options.seed, options.size_x, options.size_y = seed, width, height
                result = c.c_void_p()
                self._native.call("generate_ex", self._handle, c.byref(options), parameters, c.byref(result))
                try:
                    return self._read_result(result)
                finally:
                    self._native.result_free(result)
            finally:
                self._native.parameters_free(parameters)

    def _read_result(self, result):
        image, stats = n.sized(n.Image), n.sized(n.Stats)
        self._native.call("result_query_image", result, c.byref(image))
        self._native.call("result_query_stats", result, c.byref(stats))
        count = c.c_uint32()
        self._native.call("result_output_count", result, c.byref(count))
        outputs = []
        for index in range(count.value):
            slot = n.sized(n.Slot)
            self._native.call("result_output_slot", result, index, c.byref(slot))
            parts, bits = (), ()
            if slot.kind == OutputKind.PART:
                parts = tuple(self._read_part(result, index, entry) for entry in range(slot.element_count))
            else:
                old = n.sized(n.Output)
                self._native.call("result_output_get", result, index, c.byref(old))
                size = old.element_count * old.stride_floats
                bits = struct.unpack("=" + "I" * size, c.string_at(old.values, size * 4)) if size else ()
            outputs.append(Output(slot.output_index, slot.owner, n.text(slot.name), OutputKind(slot.kind),
                bool(slot.is_list), slot.element_count, bits, parts))
        self._native.call("result_resource_count", result, c.byref(count))
        resources = tuple(self._read_resource(result, resource_id) for resource_id in range(1, count.value + 1))
        return GeneratedImage(image.width, image.height, image.row_stride, image.pixel_format, image.alpha_mode,
            image.origin, c.string_at(image.pixels, image.byte_count), tuple(outputs),
            Statistics(stats.fuel_budget, stats.fuel_remaining, stats.fuel_used, stats.image_byte_count, stats.output_element_count), resources)

    def _read_part(self, result, slot, entry):
        value = n.sized(n.Part)
        self._native.call("result_output_part", result, slot, entry, c.byref(value))
        fields = {}
        for field in PartSnapshot.__dataclass_fields__:
            item = getattr(value, field)
            if isinstance(item, n.Point):
                item = Point(item.x, item.y)
            elif isinstance(item, n.Color):
                item = Color(item.r, item.g, item.b, item.a)
            fields[field] = item
        return PartSnapshot(**fields)

    def _read_resource(self, result, resource_id):
        info = n.sized(n.Resource)
        self._native.call("result_resource_get", result, resource_id, c.byref(info))
        rings, regions = [], []
        for index in range(info.ring_count):
            ring = n.sized(n.Ring)
            self._native.call("result_resource_ring", result, resource_id, index, c.byref(ring))
            vertices = (n.Point * ring.vertex_count)()
            if ring.vertex_count:
                self._native.call("result_resource_ring_copy_vertices", result, resource_id, index, vertices, c.sizeof(vertices))
            rings.append(Ring(ring.ring_index, ring.parent_ring, bool(ring.is_hole), tuple(Point(p.x, p.y) for p in vertices)))
        for index in range(info.region_count):
            region = n.sized(n.Region)
            self._native.call("result_resource_region", result, resource_id, index, c.byref(region))
            holes = []
            for hole in range(region.hole_count):
                value = c.c_uint32()
                self._native.call("result_resource_region_hole", result, resource_id, index, hole, c.byref(value))
                holes.append(value.value)
            regions.append(Region(region.region_index, region.outer_ring, tuple(holes), region.source_id, region.area,
                Point(region.center.x, region.center.y), Point(region.bounds_origin.x, region.bounds_origin.y),
                Point(region.bounds_size.x, region.bounds_size.y)))
        return Resource(info.resource_id, info.kind, info.pixel_format, info.width, info.height, info.row_stride,
            c.string_at(info.data, info.byte_count) if info.byte_count else b"", Point(info.scale.x, info.scale.y),
            info.vertex_count, info.edge_count, tuple(rings), tuple(regions))
