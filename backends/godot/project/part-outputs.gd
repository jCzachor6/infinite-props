# Full typed values, normalized only for the shared C++/C test observations.
extends SceneTree

var failed := false

func demand(ok: bool, message: String) -> void:
	if not ok:
		failed = true
		printerr(message)

func bits(value: float) -> int:
	return PackedFloat32Array([value]).to_byte_array().decode_u32(0)

func point(value: Vector2) -> Array:
	return [bits(value.x), bits(value.y)]

# Godot's JSON reader represents integral wire bits as doubles. Normalize only
# exact 32-bit integers, keeping equality strict for every bit and array entry.
func reference(value: Variant) -> Variant:
	if value is float:
		demand(is_finite(value) and value == floor(value) and value >= -2147483648.0 and value <= 4294967295.0, "non-integral fixture field")
		return int(value)
	if value is Array:
		var result: Array = []
		for element in value:
			result.append(reference(element))
		return result
	if value is Dictionary:
		var result: Dictionary = {}
		for key in value:
			result[key] = reference(value[key])
		return result
	return value

func observation(image: Image, slots: Array, stores: Array) -> Dictionary:
	var outputs: Array = []
	for i in range(slots.size()):
		var slot: Dictionary = slots[i]
		demand(slot.index == i, "slot index")
		var stride: int = [2, 4, 3, 0][slot.kind]
		var values: Array = []
		if stride != 0:
			for index in range(0, slot.values.size(), stride):
				var value: Array = []
				for c in range(stride):
					value.append(bits(slot.values[index + c]))
				values.append(value)
		var parts: Array = []
		for p in slot.parts:
			demand(p.shape_resource > 0 and p.shape_resource <= stores.size(), "shape resource ID")
			demand(p.texture_resource >= 0 and p.texture_resource <= stores.size(), "texture resource ID")
			parts.append({"slot": p.output_index, "entry": p.element_index, "id": p.id, "source_id": p.source_id,
				"shape": [p.shape_kind, p.shape_resource, p.region_index],
				"appearance": [p.appearance_kind, p.texture_resource, p.tint[0], p.tint[1], p.tint[2], p.tint[3]],
				"source_mapping": [p.source_d4, p.source_origin_x, p.source_origin_y],
				"texture_mapping": [p.texture_d4, p.texture_origin_x, p.texture_origin_y],
				"transform": [p.d4, p.translation_x, p.translation_y],
				"source_scale": point(p.source_scale), "target_scale": point(p.target_scale), "anchor": point(p.source_anchor),
				"center": point(p.center), "bounds_origin": point(p.bounds_origin), "bounds_size": point(p.bounds_size)})
		demand(slot.count == (parts.size() if stride == 0 else values.size()), "slot count")
		outputs.append({"name": slot.name, "owner": slot.owner, "kind": slot.kind, "list": slot.list, "values": values, "parts": parts})
	var resources: Array = []
	for i in range(stores.size()):
		var r: Dictionary = stores[i]
		demand(r.id == i + 1 and r.byte_count == r.data.size(), "resource metadata")
		if r.kind == 2:
			resources.append({"kind": "raster", "format": r.pixel_format, "size": [r.width, r.height],
				"scale": point(r.scale), "stride": r.row_stride, "bytes": Array(r.data)})
		else:
			demand(r.kind == 1, "resource kind")
			var rings: Array = []
			for index in range(r.rings.size()):
				var ring: Dictionary = r.rings[index]
				demand(ring.index == index, "ring index")
				var vertices: Array = []
				for v in ring.vertices:
					vertices.append(point(v))
				rings.append({"parent": ring.parent_ring, "hole": ring.is_hole, "vertices": vertices})
			var regions: Array = []
			for index in range(r.regions.size()):
				var region: Dictionary = r.regions[index]
				demand(region.index == index, "region index")
				regions.append({"outer": region.outer_ring, "holes": region.holes, "source_id": region.source_id, "area": bits(region.area),
					"center": point(region.center), "bounds_origin": point(region.bounds_origin), "bounds_size": point(region.bounds_size)})
			resources.append({"kind": "geometry", "vertex_count": r.vertex_count, "edge_count": r.edge_count, "rings": rings, "regions": regions})
	return {"schema": 1, "size": [image.get_width(), image.get_height()], "rgba": Array(image.get_data()), "outputs": outputs, "resources": resources}

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 1:
		printerr("usage: part-outputs.gd -- fixture-directory")
		quit(2)
		return
	var cases: Array = JSON.parse_string(FileAccess.get_file_as_string(args[0].path_join("part-bindings.json")))
	demand(cases.size() == 23, "part fixture count")
	for item in cases:
		var folder: String = args[0].path_join(item.directory)
		var expected: Dictionary = reference(JSON.parse_string(FileAccess.get_file_as_string(folder.path_join("expected.json"))))
		var asset := IpropAsset.new()
		if asset.load_file(folder.path_join("root.iprop")) != OK:
			printerr("part load failed: ", item.name)
			quit(1)
			return
		var image: Image = asset.generate(int(item.seed), int(item.size[0]), int(item.size[1]))
		if image == null:
			printerr("part generate failed: ", item.name)
			quit(1)
			return
		var slots: Array = asset.get_output_slots()
		var stores: Array = asset.get_resources()
		var actual: Dictionary = observation(image, slots, stores)
		if actual != expected:
			var diagnostic := FileAccess.open(folder.path_join("godot-observed.json"), FileAccess.WRITE)
			if diagnostic:
				diagnostic.store_string(JSON.stringify(actual))
		demand(actual == expected, "complete C++/C part mismatch: " + item.name)
		for r in stores:
			demand(is_same(asset.get_resource(r.id), r), "one shared table entry per ID")
		demand(asset.get_resource(0).is_empty() and asset.get_resource(stores.size() + 1).is_empty(), "invalid resource ID")
		if item.name == "geometry-raster-holes":
			demand(asset.generate(int(item.seed), -1, 0) == null, "invalid generation must fail")
			demand(is_same(asset.get_resources(), stores), "failure preserves prior resource table")
		asset.generate(int(item.seed) ^ 0xffffffff, int(item.size[0]), int(item.size[1]))
		demand(asset.load_file(folder.path_join("root.iprop")) == OK, "reload")
		asset = null
		demand(observation(image, slots, stores) == expected, "retained result after generation, reload and free: " + item.name)
	if not failed:
		print("Godot typed parts: PASS 23 complete C++/C fixtures and independent result lifetime")
	quit(1 if failed else 0)
