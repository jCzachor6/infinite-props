"""Bounded independent structural/resource preflight for frozen .iprop 0.1-0.4.

Opcode/type-flow validation is evidenced by the recorded frozen ipc compile,
which invokes runtime loadFile/validate. This parser does not claim to replace it.
"""
import math
import struct
import zlib


class PreflightError(ValueError):
    pass


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, size):
        if size < 0 or size > len(self.data) - self.pos:
            raise PreflightError("unexpected end of binary section")
        result = self.data[self.pos:self.pos + size]
        self.pos += size
        return result

    def number(self, format):
        return struct.unpack('<' + format, self.take(struct.calcsize('<' + format)))[0]

    def done(self):
        if self.pos != len(self.data):
            raise PreflightError("trailing section bytes")


def header(data):
    if len(data) < 20 or data[:4] != b'IPRP':
        raise PreflightError("bad .iprop header")
    major, minor, flags, count, reserved = struct.unpack_from('<HHIII', data, 4)
    if major != 0 or not 1 <= minor <= 4 or flags & ~3 or reserved or not 1 <= count <= 255:
        raise PreflightError("unsupported/invalid .iprop header")
    if flags & 2 and minor != 4:
        raise PreflightError("compression requires format 0.4")
    return major, minor, flags, count


def preflight(raw, limits):
    cap = limits['max_compiled_binary_bytes']
    if len(raw) > cap:
        raise PreflightError("compiled binary exceeds byte ceiling")
    outer = header(raw)
    compressed = bool(outer[2] & 2)
    if compressed:
        if len(raw) < 24:
            raise PreflightError("truncated compressed envelope")
        size = struct.unpack_from('<I', raw, 20)[0]
        if not 20 + 12 * outer[3] <= size <= cap:
            raise PreflightError("decoded binary size outside budget")
        stream = zlib.decompressobj()
        data = stream.decompress(raw[24:], size + 1)
        if len(data) != size or not stream.eof or stream.unused_data or stream.unconsumed_tail:
            raise PreflightError("invalid compressed payload length/end")
        inner = header(data)
        if inner[2] & 2 or inner[2] != outer[2] & 1 or inner[3] != outer[3]:
            raise PreflightError("compressed envelope and inner header differ")
    else:
        data = raw
    major, minor, flags, count = header(data)
    table_end = 20 + 12 * count
    if table_end > len(data):
        raise PreflightError("truncated section table")
    sections, spans = {}, []
    for index in range(count):
        kind, start, length = struct.unpack_from('<III', data, 20 + 12 * index)
        if kind not in (1, 2, 3, 4) or kind in sections or start < table_end or start + length > len(data):
            raise PreflightError("invalid section bounds/id")
        sections[kind] = data[start:start + length]
        spans.append((start, start + length))
    if not {1, 2, 3} <= set(sections) or (4 in sections) != bool(flags & 1):
        raise PreflightError("required/debug section mismatch")
    spans.sort()
    if any(a[1] > b[0] for a, b in zip(spans, spans[1:])):
        raise PreflightError("overlapping sections")
    pool = Reader(sections[2])
    string_count = pool.number('I')
    if string_count > 65536:
        raise PreflightError("string pool count out of range")
    names = [pool.take(pool.number('I')).decode('utf-8', 'strict') for _ in range(string_count)]
    for _ in range(3):
        elements = pool.number('I')
        if elements > 65536:
            raise PreflightError("constant pool count out of range")
        pool.take(4 * elements)
    pool.done()

    def name(index):
        if index >= len(names):
            raise PreflightError("name index outside pool")
        return names[index]

    def inputs(reader):
        result = []
        for _ in range(reader.number('H')):
            kind, index = reader.number('B'), reader.number('I')
            if kind not in (0, 1, 2):
                raise PreflightError("unknown uniform type")
            value = reader.take(4)
            default = ('#' + value.hex()) if kind == 0 else struct.unpack('<f' if kind == 1 else '<I', value)[0]
            result.append({'name': name(index), 'type': ['color', 'f32', 'u32'][kind], 'default': default, 'default_bits': struct.unpack('<I', value)[0]})
        if len({entry['name'] for entry in result}) != len(result):
            raise PreflightError("duplicate uniform name")
        return result

    def outputs(reader, owner):
        result = []
        for _ in range(reader.number('H')):
            kind, is_list = reader.number('B'), reader.number('B')
            if kind > 2 or is_list > 1 or reader.take(2) != b'\0\0':
                raise PreflightError("invalid output declaration")
            result.append({'owner': owner, 'name': name(reader.number('I')), 'kind': ['point', 'rect', 'circle'][kind], 'is_list': bool(is_list)})
        if len({entry['name'] for entry in result}) != len(result):
            raise PreflightError("duplicate owner output name")
        return result

    meta = Reader(sections[1])
    width, height = meta.number('I'), meta.number('I')
    origin = [meta.number('f'), meta.number('f')]
    variants, archetypes, fuel = meta.number('I'), meta.number('I'), meta.number('Q')
    meta_flags = meta.number('H')
    if meta_flags & ~1 or meta.take(6) != b'\0' * 6:
        raise PreflightError("invalid metadata flags/reserved")
    if meta_flags & 1:
        meta.take(16)
    root_inputs, slots = inputs(meta), outputs(meta, 0)
    meta.done()
    if not 1 <= width <= limits['max_root_width'] or not 1 <= height <= limits['max_root_height'] or width * height > limits['max_root_pixels']:
        raise PreflightError("root dimensions exceed resource ceiling")
    if variants < 1 or archetypes < 1 or not 1 <= fuel <= limits['max_declared_fuel'] or not all(math.isfinite(x) for x in origin):
        raise PreflightError("invalid metadata counts/fuel/origin")
    code = Reader(sections[3])
    function_count = code.number('I')
    if not 1 <= function_count <= 1024:
        raise PreflightError("function count out of range")
    stream_end = 0
    for _ in range(function_count):
        index, offset, length = code.number('I'), code.number('I'), code.number('I')
        if index != 0xffffffff:
            name(index)
        if offset != stream_end or length % 4:
            raise PreflightError("invalid instruction stream layout")
        stream_end += length
        for _ in range(3):
            types = code.take(code.number('H'))
            if any(value > 3 or (value == 3 and minor < 3) for value in types):
                raise PreflightError("invalid function value type")
    code.take(stream_end)
    part_count = code.number('I')
    if part_count > limits['max_part_count']:
        raise PreflightError("part count exceeds resource ceiling")
    parts = []
    for index in range(part_count):
        function, part_width, part_height = code.number('I'), code.number('I'), code.number('I')
        part_origin = [code.number('f'), code.number('f')]
        if function >= function_count or not 1 <= part_width <= limits['max_part_width'] or not 1 <= part_height <= limits['max_part_height'] or not all(math.isfinite(x) for x in part_origin):
            raise PreflightError("part geometry/function outside bounds")
        part_inputs, part_outputs = inputs(code), outputs(code, index + 1)
        slots.extend(part_outputs)
        parts.append({'owner': index + 1, 'width': part_width, 'height': part_height, 'origin': part_origin, 'inputs': part_inputs, 'outputs': part_outputs})
    code.done()
    if len(slots) > limits['max_output_elements_per_render']:
        raise PreflightError("declared slot count exceeds output ceiling")
    for index, slot in enumerate(slots):
        slot['slot'] = index
    return {'compressed': compressed, 'format_version': f'{major}.{minor}', 'expanded_bytes': len(data), 'width': width, 'height': height, 'origin': origin, 'variants': variants, 'archetypes': archetypes, 'fuel': fuel, 'inputs': root_inputs, 'parts': parts, 'output_slots': slots, 'semantic_validation_evidence': 'recorded ipc compilation calls frozen loadFile/validate before returning bytes', 'structural_preflight': 'independent bounded .iprop parser'}
