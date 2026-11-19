"""Owned results and concurrent generation for the preview model API."""
import ctypes as c
from dataclasses import dataclass, replace
from pathlib import Path
from threading import RLock
import weakref

from . import (_native as n, _input, _integer, _set_inputs, AuxiliaryImage,
               CancellationToken, ImageKind, ImageSource)

MAX_LOD = 7


@dataclass(frozen=True)
class ModelLibrary:
    api: tuple
    max_vertices: int
    max_triangles: int
    max_textures: int
    max_texture_side: int
    max_texels: int
    max_lod: int
    texture_stamp: int


def model_library(library=None):
    native = n.ModelNative(library)
    i = native.info
    return ModelLibrary((i.api_major, i.api_minor), i.max_vertices, i.max_triangles,
                        i.max_textures, i.max_texture_side, i.max_texels, MAX_LOD, native.stamp())


@dataclass(frozen=True)
class ModelMetadata:
    size: tuple
    units_per_meter: float
    fuel: int
    container_flags: int


@dataclass(frozen=True)
class SkinProgram:
    index: int
    name: str
    width: int
    height: int


@dataclass(frozen=True)
class KnownTexture:
    key: int
    fuel: int
    work: int
    image_kinds: int

    def __post_init__(self):
        for value in (self.key, self.fuel, self.work):
            _integer(value, 0, 0xffffffffffffffff)
        _integer(self.image_kinds, 0, 0xffffffff)

    @property
    def key_hex(self):
        return f"{self.key:016x}"


@dataclass(frozen=True)
class ModelTexture(KnownTexture):
    index: int
    name: str
    width: int
    height: int
    asset_width: int
    asset_height: int
    repeats: bool
    reused: bool
    row_stride: int
    pixel_format: int
    alpha_mode: int
    origin: int
    rgba: bytes
    images: tuple

    def to_pillow(self):
        from PIL import Image
        return Image.frombytes("RGBA", (self.width, self.height), self.rgba)


@dataclass(frozen=True)
class Surface:
    index: int
    texture_index: int
    roughness: float
    metallic: float
    emission: float
    alpha_mode: int
    alpha_cutoff: float
    two_sided: bool
    crisp: bool
    positions: bytes
    normals: bytes
    tangents: bytes
    uvs: bytes
    indices: bytes

    def floats(self, name):
        if name not in ("positions", "normals", "tangents", "uvs"):
            raise ValueError("Unknown surface float array")
        return memoryview(getattr(self, name)).cast("f")


@dataclass(frozen=True)
class ModelOutput:
    index: int
    name: str
    kind: int
    is_list: bool
    value_count: int
    floats_per_value: int
    value_bits: tuple

    @property
    def values(self):
        import struct
        return tuple(struct.unpack("=f", struct.pack("=I", word))[0] for word in self.value_bits)


@dataclass(frozen=True)
class ModelStatistics:
    vertex_count: int
    triangle_count: int
    fuel_budget: int
    fuel_used: int
    work_budget: int
    work_used: int


@dataclass(frozen=True)
class GeneratedModel:
    bounds_min: tuple
    bounds_max: tuple
    size: tuple
    units_per_meter: float
    statistics: ModelStatistics
    surfaces: tuple
    textures: tuple
    outputs: tuple


class TextureCache:
    def __init__(self):
        self._lock = RLock()
        self._entries = {}

    def _snapshot(self):
        with self._lock:
            return self._entries.copy()

    def known(self):
        return tuple(KnownTexture(t.key, t.fuel, t.work, t.image_kinds)
                     for t in self._snapshot().values())

    def get(self, key):
        with self._lock:
            return self._entries.get(key)

    def discard(self, key):
        with self._lock:
            self._entries.pop(key, None)

    def clear(self):
        with self._lock:
            self._entries.clear()

    def __len__(self):
        with self._lock:
            return len(self._entries)

    def _store(self, textures):
        with self._lock:
            self._entries.update((t.key, t) for t in textures if not t.reused)


def _query(native, operation, descriptor, *args):
    view = n.sized(descriptor)
    native.call("model_api_" + operation, *args, c.byref(view))
    return view


def _copy_texture(native, result, index, snapshot):
    v = _query(native, "result_texture", n.TextureView, result, index)
    images = []
    for i in range(v.image_count):
        a = _query(native, "result_texture_image", n.AuxImage, result, index, i)
        images.append(AuxiliaryImage(i, ImageKind(a.kind), ImageSource(a.source), n.text(a.name),
                                    a.width, a.height, a.row_stride, a.pixel_format, a.alpha_mode,
                                    a.origin, c.string_at(a.pixels, a.byte_count)))
    texture = ModelTexture(v.key, v.fuel, v.work, v.image_kinds, index, n.text(v.name), v.width,
                           v.height, v.asset_width, v.asset_height, bool(v.repeats), bool(v.reused),
                           v.row_stride, v.pixel_format, v.alpha_mode, v.origin,
                           c.string_at(v.pixels, v.byte_count) if v.byte_count else b"", tuple(images))
    if v.reused and snapshot is not None:
        stored = snapshot[v.key]
        return replace(texture, rgba=stored.rgba, images=stored.images, row_stride=stored.row_stride,
                       pixel_format=stored.pixel_format, alpha_mode=stored.alpha_mode, origin=stored.origin)
    return texture


def _copy_result(native, result, snapshot):
    r = _query(native, "result_query", n.ResultInfo, result)
    surfaces, outputs = [], []
    for i in range(r.surface_count):
        s = _query(native, "result_surface", n.SurfaceView, result, i)
        arrays = [c.string_at(data, length * 4) if length else b"" for data, length in
                  ((s.positions, 3*s.vertex_count), (s.normals, 3*s.vertex_count),
                   (s.tangents, 4*s.vertex_count), (s.uvs, 2*s.vertex_count), (s.indices, s.index_count))]
        surfaces.append(Surface(i, s.texture_index, s.roughness, s.metallic, s.emission, s.alpha_mode,
                                s.alpha_cutoff, bool(s.two_sided), bool(s.crisp), *arrays))
    for i in range(r.output_count):
        o = _query(native, "result_output", n.OutputView, result, i)
        size = o.value_count * o.floats_per_value
        bits = tuple((c.c_uint32 * size).from_address(o.values)) if size else ()
        outputs.append(ModelOutput(i, n.text(o.name), o.kind, bool(o.list), o.value_count, o.floats_per_value, bits))
    textures = tuple(_copy_texture(native, result, i, snapshot) for i in range(r.texture_count))
    stats = ModelStatistics(r.vertex_count, r.triangle_count, r.fuel_budget, r.fuel_used, r.work_budget, r.work_used)
    return GeneratedModel(tuple(r.bounds_min), tuple(r.bounds_max), tuple(r.size), r.units_per_meter,
                          stats, tuple(surfaces), textures, tuple(outputs))


class _ModelOwner:
    """Each in-flight call retains the owner until it releases its parameters."""
    def __init__(self, native, data):
        self.handle = c.c_void_p()
        native.call("model_api_model_load", c.create_string_buffer(data), len(data), c.byref(self.handle))
        weakref.finalize(self, native.model_api_model_free, self.handle)


class Model:
    def __init__(self, source, *, library=None):
        data = bytes(source) if isinstance(source, (bytes, bytearray, memoryview)) else Path(source).read_bytes()
        self._lock = RLock()
        self._native = native = n.ModelNative(library)
        self._owner = _ModelOwner(native, data)
        handle = self._owner.handle
        m = _query(native, "model_query", n.ModelInfo, handle)
        self.metadata = ModelMetadata((m.size_x, m.size_y, m.size_z), m.units_per_meter, m.fuel, m.container_flags)
        self.inputs = tuple(_input(_query(native, "model_input_get", n.Input, handle, i)) for i in range(m.input_count))
        skins = [_query(native, "model_skin_program", n.SkinProgramInfo, handle, i) for i in range(m.skin_program_count)]
        self.skin_programs = tuple(SkinProgram(s.program_index, n.text(s.name), s.width, s.height) for s in skins)

    def close(self):
        with self._lock:
            self._owner = None

    def __enter__(self):
        with self._lock:
            if self._owner is None:
                raise RuntimeError("Model is closed")
        return self

    def __exit__(self, *exc):
        self.close()

    def cancellation_token(self):
        return CancellationToken(_native=self._native)

    def generate(self, seed=0, *, inputs=None, lod=0, textures=None, known=None,
                 work_limit=0, timeout_ns=0, cancellation=None):
        native = self._native
        options = n.sized(n.GenerateOptions)
        options.seed = _integer(seed, 0, 0xffffffff)
        options.lod = _integer(lod, 0, 0xffffffff)
        limits = n.sized(n.Limits)
        limits.work_limit = _integer(work_limit, 0, 0xffffffffffffffff)
        limits.timeout_ns = _integer(timeout_ns, 0, 0xffffffffffffffff)
        if cancellation is not None:
            if not isinstance(cancellation, CancellationToken) or cancellation._native.library._handle != native.library._handle:
                raise ValueError("Cancellation token must use this runtime library")
            limits.cancellation = cancellation._handle.value

        snapshot = textures._snapshot() if textures is not None else None
        records = list(snapshot.values()) if snapshot is not None else []
        records.extend(known or ())
        array = (n.KnownTexture * len(records))(*(n.KnownTexture(t.key, t.fuel, t.work, t.image_kinds, 0)
                                                 for t in records))
        options.known_textures = c.cast(array, c.c_void_p)
        options.known_texture_count = len(records)
        parameters, result = c.c_void_p(), c.c_void_p()
        with self._lock:
            owner = self._owner
            if owner is None:
                raise RuntimeError("Model is closed")
            native.call("model_api_model_parameters_create", owner.handle, c.byref(parameters))
        try:
            _set_inputs(native, parameters, inputs)
            native.call("model_api_generate", owner.handle, c.byref(options), parameters, c.byref(limits), c.byref(result))
            model = _copy_result(native, result, snapshot)
            if textures is not None:
                textures._store(model.textures)
            return model
        finally:
            native.model_api_result_free(result)
            native.parameters_free(parameters)
