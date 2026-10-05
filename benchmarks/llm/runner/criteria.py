"""Independent public predicates and small frozen numeric/raster oracles.

No compiled generated source is used to construct expected pixels/outputs.
Required source features and visual judgment remain separate manual reviews.
"""
import ast
import math
import struct


def f32(value):
    return struct.unpack('<f', struct.pack('<f', float(value)))[0]


def bits(value):
    return struct.unpack('<I', struct.pack('<f', float(value)))[0]


def from_bits(value):
    if type(value) is not int or not 0 <= value <= 0xffffffff:
        raise ValueError('output component is not a binary32 bit pattern')
    return struct.unpack('<f', struct.pack('<I', value))[0]


def formula(value, environment):
    if not isinstance(value, str):
        return value
    def visit(node):
        if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)):
            return node.value
        if isinstance(node, ast.Name) and node.id in environment:
            return environment[node.id]
        if isinstance(node, ast.BinOp) and isinstance(node.op, (ast.Add, ast.Sub, ast.Mult, ast.Div)):
            left, right = visit(node.left), visit(node.right)
            if isinstance(node.op, ast.Add): return f32(left + right)
            if isinstance(node.op, ast.Sub): return f32(left - right)
            if isinstance(node.op, ast.Mult): return f32(left * right)
            return f32(left / right)
        raise ValueError('unsupported public formula')
    return visit(ast.parse(value, mode='eval').body)


def hash32(value):
    value &= 0xffffffff
    value ^= value >> 16
    value = value * 0x7feb352d & 0xffffffff
    value ^= value >> 15
    value = value * 0x846ca68b & 0xffffffff
    return value ^ (value >> 16)


def unit(state):
    state = state + 0x9e3779b9 & 0xffffffff
    return state, (hash32(state) >> 8) / 16777216.0


def environment(case, schema):
    result = {entry['name']: entry['default'] for entry in schema['inputs']}
    result.update(case['uniforms'])
    result.update(seed=case['seed'], variant=case['variant'], archetype=case['archetype'])
    return result


def normalize_outputs(raw, schema, limit):
    outputs, slots = raw.get('outputs'), schema['output_slots']
    if not isinstance(outputs, list) or len(outputs) != len(slots):
        raise ValueError('ipgen output count differs from declared slots')
    normalized, total = [], 0
    for slot, output in zip(slots, outputs):
        if output.get('name') != slot['name'] or output.get('kind') != slot['kind'] or output.get('list') != slot['is_list']:
            raise ValueError('ipgen output metadata differs from declared slot')
        values = output.get('values')
        if not isinstance(values, list):
            raise ValueError('output values is not a list')
        total += len(values)
        if total > limit or not slot['is_list'] and len(values) > 1:
            raise ValueError('output element count exceeds ceiling/singular limit')
        keys = {'point': ['x', 'y'], 'rect': ['x', 'y', 'w', 'h'], 'circle': ['x', 'y', 'r']}[slot['kind']]
        encoded = []
        for value in values:
            row = [value[key] for key in keys]
            for item in row: from_bits(item)
            encoded.append(row)
        normalized.append({**slot, 'values_bits': encoded, 'values': [[from_bits(value) for value in row] for row in encoded], 'owner_observation': 'independent .iprop global slot/declaration table; raw ipgen omits owner'})
    return normalized


def get_output(outputs, owner, name):
    result = [value for value in outputs if value['owner'] == owner and value['name'] == name]
    if len(result) != 1:
        raise ValueError(f'missing/ambiguous output owner={owner}, name={name}')
    return result[0]


def verdict(identifier, passed, message=None, pending=False):
    return {'criterion': identifier, 'verdict': 'pending' if pending else ('pass' if passed else 'fail'), 'message': message}


def output_checks(task, case, schema, outputs):
    result, env = [], environment(case, schema)
    for rule in task['output_criteria']:
        identifier = f'output:{rule["owner"]}:{rule["name"]}'
        try:
            actual = get_output(outputs, rule['owner'], rule['name'])
            if actual['kind'] != rule['kind'] or actual['is_list'] != rule['is_list']:
                raise ValueError('kind/listness differs')
            count = len(actual['values'])
            if 'count' in rule and count != rule['count']: raise ValueError(f'count {count}, expected {rule["count"]}')
            if case['id'] in rule.get('count_by_case', {}) and count != rule['count_by_case'][case['id']]: raise ValueError('case count differs')
            if 'count_bounds' in rule and not rule['count_bounds'][0] <= count <= rule['count_bounds'][1]: raise ValueError('count outside bounds')
            expected = None
            if 'values' in rule:
                expected = rule['values'] if rule['is_list'] else ([rule['values']] if rule['values'] else [])
            elif 'values_formula' in rule:
                expected = [[formula(value, env) for value in rule['values_formula']]]
            reference = rule.get('exact_reference')
            if reference == 'normative_index_sequence_24_draws':
                state, expected = case['seed'], []
                for _ in range(12):
                    state, x = unit(state); state, y = unit(state)
                    expected.append([min(23, int(f32(x * 24))) + 4, min(23, int(f32(y * 24))) + 4])
            elif reference == 'normative_chance_sequence_16_draws':
                state, expected = case['seed'], []
                for index in range(16):
                    state, draw = unit(state)
                    if draw < f32(env['probability']): expected.append([2 + index, 16])
            elif reference == 'normative_hash_roll_then_int':
                _, draw = unit(hash32(case['seed']))
                expected = [[int(f32(4 + f32(24 * draw))), 16]]
            if expected is not None and actual['values_bits'] != [[bits(value) for value in row] for row in expected]:
                raise ValueError(f'actual components {actual["values"]}, expected {expected}')
            if rule.get('component_rule') == 'x_bits == y_bits == signed_i32_seed_to_f32_bits':
                seed = case['seed'] if case['seed'] < 0x80000000 else case['seed'] - 0x100000000
                if actual['values_bits'] != [[bits(seed), bits(seed)]]: raise ValueError('parent seed state differs')
            if 'component_bounds' in rule:
                lo, hi = rule['component_bounds']
                if any(not lo <= value <= hi for row in actual['values'] for value in row): raise ValueError('component outside bounds')
            if rule.get('components_are_integers') and any(not value.is_integer() for row in actual['values'] for value in row): raise ValueError('noninteger coordinate')
            result.append(verdict(identifier, True))
        except (ValueError, KeyError, TypeError, OverflowError) as error:
            result.append(verdict(identifier, False, str(error)))
    return result


def rgba(color):
    if color.startswith('#'): color = color[1:]
    if len(color) in (3, 4): color = ''.join(value * 2 for value in color)
    if len(color) == 6: color += 'ff'
    result = bytes.fromhex(color)
    if len(result) != 4: raise ValueError('not an RGBA8 color')
    return result


def paint_rect(canvas, width, height, box, color):
    x, y, w, h = box
    for row in range(height):
        for column in range(width):
            if x <= column + 0.5 < x + w and y <= row + 0.5 < y + h:
                at = (row * width + column) * 4
                canvas[at:at + 4] = color


def difference(actual, expected, width):
    if len(actual) != len(expected): return 'RGBA byte count differs'
    for index in range(0, len(actual), 4):
        if actual[index:index + 4] != expected[index:index + 4]:
            pixel = index // 4
            return f'pixel ({pixel % width},{pixel // width})={actual[index:index + 4].hex()}, expected {expected[index:index + 4].hex()}'
    return None


def image_checks(task, case, schema, outputs, width, height, pixels):
    result = [verdict('dimensions', [width, height] == task['image_criteria']['dimensions'])]
    env, rules = environment(case, schema), task['image_criteria']['pixel_rules']
    if any(rule['kind'] == 'overlay_rect' for rule in rules):
        expected = bytearray(width * height * 4)
        for rule in rules:
            if rule['kind'] not in ('rect_pixels_equal', 'overlay_rect'):
                raise ValueError('unhandled composition rule')
            paint_rect(expected, width, height, [rule['x'], rule['y'], rule['width'], rule['height']], rgba(rule['rgba_hex']))
        mismatch = difference(pixels, expected, width)
        return result + [verdict('composed_base_and_overlays', mismatch is None, mismatch)]
    for index, rule in enumerate(rules):
        kind, identifier = rule['kind'], f'image:{index}:{rule["kind"]}'
        try:
            expected = None
            if kind == 'pixel_equals':
                at = (rule['y'] * width + rule['x']) * 4
                if pixels[at:at + 4] != rgba(rule['rgba_hex']): raise ValueError(f'pixel ({rule["x"]},{rule["y"]}) differs')
            elif kind == 'rect_pixels_equal':
                for y in range(rule['y'], rule['y'] + rule['height']):
                    for x in range(rule['x'], rule['x'] + rule['width']):
                        at = (y * width + x) * 4
                        if pixels[at:at + 4] != rgba(rule['rgba_hex']): raise ValueError(f'pixel ({x},{y}) differs')
            elif kind in ('outside_rectangles_equals', 'outside_pixels_equals'):
                boxes = rule.get('rectangles', [[x, y, 1, 1] for x, y in rule.get('pixels', [])])
                for y in range(height):
                    for x in range(width):
                        if not any(bx <= x < bx + bw and by <= y < by + bh for bx, by, bw, bh in boxes):
                            at = (y * width + x) * 4
                            if pixels[at:at + 4] != rgba(rule['rgba_hex']): raise ValueError(f'outside pixel ({x},{y}) differs')
            elif kind == 'checkerboard':
                expected = bytearray()
                for y in range(height):
                    for x in range(width):
                        odd = (x // rule['cell_width'] + y // rule['cell_height']) % 2
                        expected.extend(rgba(rule['odd_rgba_hex'] if odd else rule['even_rgba_hex']))
            elif kind in ('case_rectangle', 'uniform_rectangle'):
                expected = bytearray(rgba(rule['background_rgba_hex']) * (width * height))
                box = [formula(rule.get('x_formula', rule.get('x')), env), rule['y'], formula(rule['width_formula'], env) if 'width_formula' in rule else env[rule['width_uniform']], formula(rule['height_formula'], env) if 'height_formula' in rule else rule['height']]
                color = rgba(rule['palette_by_variant'][case['variant']] if kind == 'case_rectangle' else env[rule['color_uniform']])
                paint_rect(expected, width, height, box, color)
            elif kind == 'points_paint_image':
                expected = bytearray(rgba(rule['background_rgba_hex']) * (width * height))
                points = get_output(outputs, rule['owner'], rule['output_name'])['values']
                for x, y in points:
                    if not math.isfinite(x) or not math.isfinite(y) or not x.is_integer() or not y.is_integer() or not 0 <= x < width or not 0 <= y < height:
                        raise ValueError('paint point outside integer canvas')
                    at = (int(y) * width + int(x)) * 4
                    expected[at:at + 4] = rgba(rule['foreground_rgba_hex'])
            elif kind == 'frozen_compositor_rect':
                destination, source = rgba(rule['destination_rgba_hex']), rgba(rule['source_rgba_hex'])
                alpha = source[3]
                mixed = bytes([(source[c] * alpha + destination[c] * (255 - alpha)) // 255 for c in range(3)] + [destination[3]])
                expected = bytearray(destination * (width * height))
                paint_rect(expected, width, height, [rule['x'], rule['y'], rule['width'], rule['height']], mixed)
            elif kind == 'frozen_part_transform':
                if rule['rot'] != 90 or rule['mirror_x'] or rule['mirror_y'] or rule['origin'] != [0, 0]:
                    result.append(verdict(identifier, False, 'oracle covers this suite rot90/origin0 cases only', pending=True)); continue
                expected = bytearray(rgba(rule['background_rgba_hex']) * (width * height))
                bx, by, bw, bh = rule['source_rectangle']
                for sy in range(by, by + bh):
                    for sx in range(bx, bx + bw):
                        x, y = rule['at'][0] + sy, rule['at'][1] + rule['part_size'][0] - 1 - sx
                        if 0 <= x < width and 0 <= y < height:
                            at = (y * width + x) * 4
                            expected[at:at + 4] = rgba(rule['foreground_rgba_hex'])
            elif kind == 'dynamic_ramp_columns':
                a, b = [rgba(value) for value in rule['palette_rgba_hex']]
                expected = bytearray()
                row = bytearray()
                for x in range(width):
                    t = f32(f32(x) / 31.0)
                    color = b if t == 1 else bytes([int(f32(a[c] * f32(1 - t) + f32(b[c] * t))) for c in range(3)] + [a[3]])
                    row.extend(color)
                expected = row * height
            elif kind == 'all_alpha_equals':
                if any(value != rule['alpha'] for value in pixels[3::4]): raise ValueError('alpha differs')
            elif kind == 'distinct_rgb_count_min':
                if len({pixels[i:i + 3] for i in range(0, len(pixels), 4)}) < rule['minimum']: raise ValueError('too few distinct RGB colors')
            elif kind == 'different_seed_images':
                result.append(verdict(identifier, False, 'evaluated after all cases', pending=True)); continue
            else:
                result.append(verdict(identifier, False, 'unimplemented public predicate', pending=True)); continue
            if expected is not None:
                mismatch = difference(pixels, expected, width)
                if mismatch: raise ValueError(mismatch)
            result.append(verdict(identifier, True))
        except (ValueError, KeyError, TypeError, OverflowError, IndexError) as error:
            result.append(verdict(identifier, False, str(error)))
    return result
