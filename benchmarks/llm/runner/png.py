"""Independent bounded PNG decoder: non-interlaced RGB/RGBA8, all five filters."""
import struct
import zlib


def decode(data, max_pixels=4096):
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('not a PNG')
    pos, payload, shape, ended = 8, bytearray(), None, False
    while pos < len(data):
        if len(data) - pos < 12:
            raise ValueError('truncated PNG chunk')
        length = struct.unpack_from('>I', data, pos)[0]
        kind = data[pos + 4:pos + 8]
        end = pos + 12 + length
        if end > len(data):
            raise ValueError('PNG chunk outside file')
        chunk = data[pos + 8:pos + 8 + length]
        if zlib.crc32(kind + chunk) & 0xffffffff != struct.unpack_from('>I', data, pos + 8 + length)[0]:
            raise ValueError('PNG checksum mismatch')
        if kind == b'IHDR':
            if shape is not None or pos != 8 or length != 13:
                raise ValueError('invalid PNG IHDR')
            width, height, depth, color, compression, filters, interlace = struct.unpack('>IIBBBBB', chunk)
            if not width or not height or width * height > max_pixels or depth != 8 or color not in (2, 6) or compression or filters or interlace:
                raise ValueError('unsupported/budget-exceeding PNG layout')
            shape = (width, height, 4 if color == 6 else 3)
        elif kind == b'IDAT':
            if shape is None:
                raise ValueError('IDAT before IHDR')
            payload.extend(chunk)
        elif kind == b'IEND':
            if length or end != len(data):
                raise ValueError('invalid/trailing PNG IEND')
            ended = True
            break
        elif kind[0] & 32 == 0 and kind != b'PLTE':
            raise ValueError('unknown critical PNG chunk')
        pos = end
    if not ended or shape is None:
        raise ValueError('unfinished PNG')
    width, height, channels = shape
    expected = height * (1 + width * channels)
    stream = zlib.decompressobj()
    raw = stream.decompress(bytes(payload), expected + 1)
    if len(raw) != expected or not stream.eof or stream.unused_data or stream.unconsumed_tail:
        raise ValueError('invalid PNG zlib length/end')
    row_size, result, previous, pos = width * channels, bytearray(), bytearray(width * channels), 0
    for _ in range(height):
        kind, row = raw[pos], bytearray(raw[pos + 1:pos + 1 + row_size])
        pos += row_size + 1
        if kind > 4:
            raise ValueError('unknown PNG filter')
        for index in range(row_size):
            left = row[index - channels] if index >= channels else 0
            up = previous[index]
            upper_left = previous[index - channels] if index >= channels else 0
            if kind == 1:
                prediction = left
            elif kind == 2:
                prediction = up
            elif kind == 3:
                prediction = (left + up) // 2
            elif kind == 4:
                p = left + up - upper_left
                distances = (abs(p - left), abs(p - up), abs(p - upper_left))
                prediction = (left, up, upper_left)[distances.index(min(distances))]
            else:
                prediction = 0
            row[index] = (row[index] + prediction) & 255
        if channels == 4:
            result.extend(row)
        else:
            for index in range(0, row_size, 3):
                result.extend(row[index:index + 3])
                result.append(255)
        previous = row
    return width, height, bytes(result)
