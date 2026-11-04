"""Compare every exported field with the shared, C++/C-verified fixtures."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import unittest

from infinite_props import Program, OutputKind

FIXTURES = Path(os.environ['IPROP_FIXTURES'])


def point(p):
    return [struct.unpack('=I', struct.pack('=f', v))[0] for v in (p.x, p.y)]


def observe(result):
    outputs, resources = [], []
    for index, slot in enumerate(result.outputs):
        assert slot.index == index
        stride = {OutputKind.POINT: 2, OutputKind.RECT: 4, OutputKind.CIRCLE: 3, OutputKind.PART: 0}[slot.kind]
        assert slot.count == (len(slot.parts) if not stride else len(slot.value_bits) // stride)
        parts = []
        for p in slot.parts:
            assert result.resource(p.shape_resource) is result.resources[p.shape_resource - 1]
            if p.texture_resource:
                assert result.resource(p.texture_resource) is result.resources[p.texture_resource - 1]
            parts.append(dict(slot=p.output_index, entry=p.element_index, id=p.id, source_id=p.source_id,
                shape=[p.shape_kind, p.shape_resource, p.region_index],
                appearance=[p.appearance_kind, p.texture_resource, p.tint.r, p.tint.g, p.tint.b, p.tint.a],
                source_mapping=[p.source_d4, p.source_origin_x, p.source_origin_y],
                texture_mapping=[p.texture_d4, p.texture_origin_x, p.texture_origin_y],
                transform=[p.d4, p.translation_x, p.translation_y],
                source_scale=point(p.source_scale), target_scale=point(p.target_scale), anchor=point(p.source_anchor),
                center=point(p.center), bounds_origin=point(p.bounds_origin), bounds_size=point(p.bounds_size)))
        outputs.append(dict(name=slot.name, owner=slot.owner, kind=int(slot.kind), list=slot.is_list,
            values=[list(slot.value_bits[i:i+stride]) for i in range(0, len(slot.value_bits), stride)] if stride else [], parts=parts))
    for index, resource in enumerate(result.resources):
        assert resource.id == index + 1
        assert resource.byte_count == len(resource.data)
        if resource.kind == 2:
            assert not resource.rings and not resource.regions
            resources.append(dict(kind='raster', format=resource.pixel_format, size=[resource.width, resource.height],
                scale=point(resource.scale), stride=resource.row_stride, bytes=list(resource.data)))
        else:
            assert resource.kind == 1 and not resource.data
            resources.append(dict(kind='geometry', vertex_count=resource.vertex_count, edge_count=resource.edge_count,
                rings=[dict(parent=r.parent_ring, hole=r.is_hole, vertices=[point(p) for p in r.vertices]) for r in resource.rings],
                regions=[dict(outer=r.outer_ring, holes=list(r.holes), source_id=r.source_id,
                    area=struct.unpack('=I', struct.pack('=f', r.area))[0], center=point(r.center),
                    bounds_origin=point(r.bounds_origin), bounds_size=point(r.bounds_size)) for r in resource.regions]))
    return dict(schema=1, size=[result.width, result.height], rgba=list(result.rgba), outputs=outputs, resources=resources)


class PartOutputTests(unittest.TestCase):
    def test_cli_refuses_partial_json_before_writing_an_image(self):
        for name in ['geometry-raster-holes', 'empty']:
            directory = FIXTURES / ('part-binding-' + name)
            output = directory / 'forbidden-cli.png'
            output.unlink(missing_ok=True)
            call = subprocess.run([sys.executable, '-m', 'infinite_props', str(directory / 'root.iprop'), str(output)],
                text=True, capture_output=True)
            self.assertEqual(call.returncode, 2)
            self.assertIn('part outputs are unsupported by the CLI JSON format', call.stderr)
            self.assertEqual(call.stdout, '')
            self.assertFalse(output.exists())

    def test_complete_cpp_capi_parity_and_independent_lifetime(self):
        cases = json.loads((FIXTURES / 'part-bindings.json').read_text())
        self.assertEqual(len(cases), 23)
        for case in cases:
            with self.subTest(case=case['name']):
                directory = FIXTURES / case['directory']
                expected = json.loads((directory / 'expected.json').read_text())
                with Program(directory / 'root.iprop') as program:
                    result = program.generate(case['seed'], width=case['size'][0], height=case['size'][1])
                    self.assertEqual(observe(result), expected)
                    program.generate(case['seed'] ^ 0xffffffff, width=case['size'][0], height=case['size'][1])
                self.assertEqual(observe(result), expected, 'result owns every resource after native result/program free and another generation')
                self.assertEqual(result.statistics.fuel_used, result.statistics.fuel_budget - result.statistics.fuel_remaining)
                self.assertEqual(result.statistics.output_element_count, sum(s.count for s in result.outputs))
                with self.assertRaises(ValueError):
                    result.resource(0)
                with self.assertRaises(ValueError):
                    result.resource(len(result.resources) + 1)


if __name__ == '__main__':
    unittest.main()
