"""C API 0.4 / ABI 0.5. Pointer-bearing records use native alignment."""
import ctypes as c
from ctypes.util import find_library
import os
import sys


class Color(c.Structure):
    _fields_ = [(name, c.c_uint8) for name in ("r", "g", "b", "a")]


class View(c.Structure):
    _fields_ = [("data", c.c_void_p), ("byte_count", c.c_uint32)]


class LibraryInfo(c.Structure):
    _fields_ = [(name, c.c_uint32) for name in ("struct_size", "product_major", "product_minor", "product_patch",
        "api_major", "api_minor", "abi_major", "abi_minor", "format_major", "format_minor_min", "format_minor_max")] + [("capabilities", c.c_uint64)]


class Error(c.Structure):
    _fields_ = [(name, c.c_uint32) for name in ("struct_size", "status", "reason", "stage")] + [
        ("byte_offset", c.c_uint64)] + [(name, c.c_uint32) for name in
        ("location_flags", "offset_space", "function_index", "pc", "debug_line")] + [
        ("message_buffer", c.c_void_p), ("message_capacity", c.c_uint32),
        ("message_required_bytes", c.c_uint64), ("message_truncated", c.c_uint32)]


class Meta(c.Structure):
    _fields_ = [("struct_size", c.c_uint32), ("size_x", c.c_uint32), ("size_y", c.c_uint32),
        ("origin_x", c.c_float), ("origin_y", c.c_float), ("fuel", c.c_uint64),
        ("has_emission", c.c_uint32), ("emission_color", Color),
        ("emission_offset_x", c.c_float), ("emission_offset_y", c.c_float), ("emission_radius", c.c_float),
        ("format_major", c.c_uint32), ("format_minor", c.c_uint32), ("container_flags", c.c_uint32)]


class Input(c.Structure):
    _fields_ = [(name, c.c_uint32) for name in ("struct_size", "flags", "input_index", "type")] + [
        ("name", View), ("color_default", Color), ("f32_default", c.c_float), ("u32_default", c.c_uint32)]


class Parameter(c.Structure):
    _fields_ = [(name, c.c_uint32) for name in ("struct_size", "flags", "type")] + [
        ("color", Color), ("f32", c.c_float), ("u32", c.c_uint32)]


class Options(c.Structure):
    _fields_ = [(name, c.c_uint32) for name in ("struct_size", "flags", "seed", "size_x", "size_y")]


class Image(c.Structure):
    _fields_ = [(name, c.c_uint32) for name in ("struct_size", "flags", "width", "height")] + [
        ("byte_count", c.c_uint64), ("row_stride", c.c_uint64), ("pixel_format", c.c_uint32),
        ("alpha_mode", c.c_uint32), ("origin", c.c_uint32), ("pixels", c.c_void_p)]


class Stats(c.Structure):
    _fields_ = [("struct_size", c.c_uint32), ("flags", c.c_uint32)] + [(name, c.c_uint64) for name in
        ("fuel_budget", "fuel_remaining", "fuel_used", "image_byte_count", "output_element_count")]


class Output(c.Structure):
    _fields_ = [(name, c.c_uint32) for name in ("struct_size", "flags", "output_index", "owner", "kind",
        "is_list", "element_count", "stride_floats")] + [("name", View), ("values", c.c_void_p)]


def sized(kind):
    value = kind()
    value.struct_size = c.sizeof(kind)
    return value


def text(view):
    return c.string_at(view.data, view.byte_count).decode("utf-8") if view.byte_count else ""


class IpropError(RuntimeError):
    def __init__(self, status, error, message):
        super().__init__(message)
        self.status = status
        for name in ("reason", "stage", "byte_offset", "location_flags", "offset_space", "function_index", "pc", "debug_line"):
            setattr(self, name, getattr(error, name))
        self.message_truncated = bool(error.message_truncated)


class Native:
    def __init__(self, path=None):
        path = path or os.environ.get("IPROP_LIBRARY") or find_library("ipropc-0.5" if sys.platform == "win32" else "ipropc")
        if not path:
            raise OSError("Set IPROP_LIBRARY or pass library= with the C ABI 0.5 library path")
        self.library = c.CDLL(os.fspath(path))
        p, u, error = c.c_void_p, c.c_uint32, c.POINTER(Error)
        signatures = {
            "library_query_ex": [c.POINTER(LibraryInfo), error],
            "program_load_ex": [p, c.c_uint64, c.POINTER(p), error],
            "program_query": [p, c.POINTER(Meta), error],
            "program_input_count": [p, c.POINTER(u), error],
            "program_input_get": [p, u, c.POINTER(Input), error],
            "parameters_create": [p, c.POINTER(p), error],
            "parameters_set_named": [p, View, c.POINTER(Parameter), error],
            "generate_ex": [p, c.POINTER(Options), p, c.POINTER(p), error],
            "result_query_image": [p, c.POINTER(Image), error],
            "result_query_stats": [p, c.POINTER(Stats), error],
            "result_output_count": [p, c.POINTER(u), error],
            "result_output_get": [p, u, c.POINTER(Output), error],
        }
        for name, args in signatures.items():
            function = getattr(self.library, "iprop_" + name)
            function.argtypes, function.restype = args, u
            setattr(self, name, function)
        for name in ("program_free", "parameters_free", "result_free"):
            function = getattr(self.library, "iprop_" + name)
            function.argtypes, function.restype = [p], None
            setattr(self, name, function)
        info = sized(LibraryInfo)
        self.call("library_query_ex", c.byref(info))
        if (info.api_major, info.abi_major, info.abi_minor) != (0, 0, 5) or info.api_minor < 4:
            raise RuntimeError("Infinite Props requires C API 0.4 / ABI 0.5")

    def call(self, name, *args):
        error = sized(Error)
        buffer = c.create_string_buffer(8192)
        error.message_buffer, error.message_capacity = c.addressof(buffer), len(buffer)
        status = getattr(self, name)(*args, c.byref(error))
        if status:
            raise IpropError(status, error, buffer.value.decode("utf-8", errors="replace"))
