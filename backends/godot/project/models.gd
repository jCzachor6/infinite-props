# Models through IpropModel and IpropModelInstance3D against the native tools
# (preview). Every model of the corpus is compiled by ipc and generated both
# by ipgen --dump and by the extension: the raw arrays, the material numbers,
# the textures and the counters must agree bit for bit. The mesh handed to
# Godot is then checked against those arrays (metres, reversed triangles,
# one material per surface), and failures against their native reasons.
#
# Usage (headless):
#   godot --headless --path . --script res://models.gd -- <source-dir> <tools-dir> <work-dir>
extends SceneTree

# Error tags of the C API (iprop.h).
const E_FORMAT := 2
const E_TRAP := 3
const E_ARGUMENT := 4
const REASON_BAD_MAGIC := 1002
const REASON_NUMERIC_RANGE := 3010
const REASON_WORK_EXHAUSTED := 3015
const REASON_CANCELLED := 3016
const STAGE_EXECUTE := 4

# The corpus of the WASM conformance (backends/html5/test/model-conformance.js).
# Inputs are written as `ipgen --uniform` takes them.
const CORPUS := [
	{"name": "primitives", "source": "backends/html5/test/fixtures/model-primitives.ips", "seeds": [0, 2, 7, 4294967295],
	 "inputs": [{}, {"stretch": "1.75", "sides": "5", "tint": "#c0392b"}, {"stretch": "0.625", "sides": "31", "rounding": "2.5"}]},
	{"name": "crate", "source": "backends/capi3d/test/crate.ips", "seeds": [0, 5, 77],
	 "inputs": [{}, {"wide": "6.5", "extra": "0"}, {"wide": "11.75", "stain": "#10a0f080"}]},
	{"name": "chair", "source": "examples/models/chair.ips", "seeds": [1, 5, 13],
	 "inputs": [{}, {"finish": "5", "fabric": "2"}]},
	{"name": "empty", "source": "backends/capi3d/test/empty.ips", "seeds": [0, 9], "inputs": [{}]},
	{"name": "materials", "source": "tests/model/corpus/materials.ips", "seeds": [1, 3, 6], "inputs": [{}]},
]

var failures := 0
var source_dir := ""
var tools := ""
var work := ""
var generations := 0
var vertices := 0

# The bytes of a GPU texture's first level: textures carry mipmaps.
func level(texture: Texture2D) -> PackedByteArray:
	var image := texture.get_image()
	image.clear_mipmaps()
	return image.get_data()

func demand(condition: bool, message: String) -> void:
	if not condition:
		printerr("FAIL ", message)
		failures += 1

func tool(name: String) -> String:
	return tools.path_join(name + (".exe" if OS.get_name() == "Windows" else ""))

# Runs a native tool; `text` is its standard output, or both streams.
func run(name: String, args: PackedStringArray, with_errors := false) -> Dictionary:
	var output: Array = []
	var code := OS.execute(tool(name), args, output, with_errors)
	return {"code": code, "text": "".join(output)}

func compile(source: String, name: String) -> String:
	var path := work.path_join(name)
	var compiled := run("ipc", [source_dir.path_join(source), "-o", path], true)
	demand(compiled.code == 0, "ipc %s: %s" % [source, compiled.text])
	return path

# The binary32 bits of a float that came from one.
func bits(value: float) -> int:
	return PackedFloat32Array([value]).to_byte_array().decode_u32(0)

func same_bits(actual: PackedFloat32Array, expected: Array) -> bool:
	if actual.size() != expected.size():
		return false
	var bytes := actual.to_byte_array()
	for i in expected.size():
		if bytes.decode_u32(i * 4) != int(expected[i]):
			return false
	return true

func same_vector(actual: Vector3, expected: Array) -> bool:
	return bits(actual.x) == int(expected[0]) and bits(actual.y) == int(expected[1]) and bits(actual.z) == int(expected[2])

# FNV-1a 64 as sixteen hex digits. GDScript ints are signed and wrap, which
# is the arithmetic the hash wants; the offset basis is 0xcbf29ce484222325.
func fnv1a64(bytes: PackedByteArray) -> String:
	var state := -3750763034362895579
	for byte in bytes:
		state = (state ^ byte) * 1099511628211
	return "%08x%08x" % [(state >> 32) & 0xffffffff, state & 0xffffffff]

# The first difference between the latest generation and a native dump.
func difference(model: IpropModel, expected: Dictionary) -> String:
	if not same_vector(model.size, expected.size) or bits(model.units_per_meter) != int(expected.units_per_meter):
		return "size or units"
	if not same_vector(model.bounds_min, expected.bounds.min) or not same_vector(model.bounds_max, expected.bounds.max):
		return "bounds"
	if model.vertex_count != int(expected.vertices) or model.triangle_count != int(expected.triangles):
		return "counts"
	if model.fuel != int(expected.fuel.budget) or model.fuel_used != int(expected.fuel.spent):
		return "fuel"
	if model.work.budget != int(expected.work.budget) or model.work.used != int(expected.work.spent):
		return "work"
	var textures: Array = model.textures
	if textures.size() != expected.textures.size():
		return "texture count"
	for t in textures.size():
		var texture: Dictionary = textures[t]
		var wanted: Dictionary = expected.textures[t]
		if texture.index != t or texture.key != wanted.key or texture.name != wanted.name:
			return "texture %d identity" % t
		if texture.width != int(wanted.width) or texture.height != int(wanted.height) or texture.repeats != wanted.repeats:
			return "texture %d size" % t
		if texture.asset_width != int(wanted.asset[0]) or texture.asset_height != int(wanted.asset[1]):
			return "texture %d declared size" % t
		var image: Image = texture.image
		if image.get_format() != Image.FORMAT_RGBA8 or image.get_size() != Vector2i(texture.width, texture.height):
			return "texture %d image layout" % t
		if fnv1a64(image.get_data()) != wanted.color:
			return "texture %d pixels" % t
		if texture.images.size() != wanted.images.size():
			return "texture %d image count" % t
		for i in texture.images.size():
			var aux: Dictionary = texture.images[i]
			if aux.kind != int(wanted.images[i].kind) or aux.name != wanted.images[i].name:
				return "texture %d image %d tags" % [t, i]
			if fnv1a64(aux.image.get_data()) != wanted.images[i].pixels:
				return "texture %d image %d pixels" % [t, i]
	var surfaces: Array = model.surfaces
	if surfaces.size() != expected.surfaces.size():
		return "surface count"
	for s in surfaces.size():
		var surface: Dictionary = surfaces[s]
		var wanted: Dictionary = expected.surfaces[s]
		if surface.index != s or surface.texture != int(wanted.texture):
			return "surface %d texture" % s
		if bits(surface.roughness) != int(wanted.roughness) or bits(surface.metallic) != int(wanted.metallic) \
				or bits(surface.emission) != int(wanted.emission) or bits(surface.alpha_cutoff) != int(wanted.cutoff):
			return "surface %d material numbers" % s
		if surface.alpha_mode != int(wanted.alpha) or surface.two_sided != wanted.two_sided or surface.crisp != wanted.crisp:
			return "surface %d material flags" % s
		if surface.vertex_count * 3 != surface.positions.size() or surface.index_count != surface.indices.size():
			return "surface %d counts" % s
		for field in ["positions", "normals", "tangents", "uvs"]:
			if not same_bits(surface[field], wanted[field]):
				return "surface %d %s" % [s, field]
		var indices: PackedInt32Array = surface.indices
		if indices.size() != wanted.indices.size():
			return "surface %d index count" % s
		for i in indices.size():
			if indices[i] != int(wanted.indices[i]):
				return "surface %d index %d" % [s, i]
	return ""

# The mesh Godot draws against the raw arrays of the same generation.
func check_mesh(model: IpropModel, mesh: ArrayMesh, label: String) -> void:
	var surfaces: Array = model.surfaces
	demand(mesh != null and mesh.get_surface_count() == surfaces.size(), label + ": a surface per material")
	if mesh == null or mesh.get_surface_count() != surfaces.size():
		return
	var units: float = model.units_per_meter
	for s in surfaces.size():
		var raw: Dictionary = surfaces[s]
		var texture: Dictionary = model.textures[raw.texture]
		var arrays := mesh.surface_get_arrays(s)
		var positions: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var normals: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
		var tangents: PackedFloat32Array = arrays[Mesh.ARRAY_TANGENT]
		var uvs: PackedVector2Array = arrays[Mesh.ARRAY_TEX_UV]
		var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
		var count: int = raw.vertex_count
		var matches: bool = positions.size() == count and normals.size() == count and tangents.size() == count * 4 \
			and uvs.size() == count and indices.size() == raw.index_count
		demand(matches, "%s surface %d: array sizes" % [label, s])
		if not matches:
			continue
		var exact := true
		var close := true
		for i in count:
			# Metres: the same binary32 division the extension makes.
			var position := Vector3(raw.positions[i * 3] / units, raw.positions[i * 3 + 1] / units, raw.positions[i * 3 + 2] / units)
			exact = exact and positions[i] == position and uvs[i] == Vector2(raw.uvs[i * 2], raw.uvs[i * 2 + 1])
			# Godot stores directions packed: equal within its precision, the sign exactly.
			var normal := Vector3(raw.normals[i * 3], raw.normals[i * 3 + 1], raw.normals[i * 3 + 2])
			var tangent := Vector3(raw.tangents[i * 4], raw.tangents[i * 4 + 1], raw.tangents[i * 4 + 2])
			close = close and normals[i].dot(normal) > 0.9999 \
				and Vector3(tangents[i * 4], tangents[i * 4 + 1], tangents[i * 4 + 2]).dot(tangent) > 0.9999 \
				and tangents[i * 4 + 3] == raw.tangents[i * 4 + 3]
		demand(exact, "%s surface %d: positions in metres and UVs" % [label, s])
		demand(close, "%s surface %d: normals and tangents" % [label, s])
		# Clockwise for Godot: the second and third index of every triangle change places.
		var reversed := true
		for i in range(0, indices.size(), 3):
			reversed = reversed and indices[i] == raw.indices[i] and indices[i + 1] == raw.indices[i + 2] \
				and indices[i + 2] == raw.indices[i + 1]
		demand(reversed, "%s surface %d: reversed triangles" % [label, s])

		var material := mesh.surface_get_material(s) as StandardMaterial3D
		demand(material != null, "%s surface %d: a StandardMaterial3D" % [label, s])
		if material == null:
			continue
		demand(mesh.surface_get_name(s) == texture.name, "%s surface %d: named after its skin" % [label, s])
		demand(bits(material.roughness) == bits(raw.roughness) and bits(material.metallic) == bits(raw.metallic),
			"%s surface %d: roughness and metallic" % [label, s])
		demand(material.albedo_texture != null and material.albedo_texture.get_image().get_size() == Vector2i(texture.width, texture.height),
			"%s surface %d: albedo texture" % [label, s])
		demand(material.normal_enabled == (texture.normal != null) and (material.normal_texture != null) == (texture.normal != null),
			"%s surface %d: normal map" % [label, s])
		demand(material.get_flag(BaseMaterial3D.FLAG_USE_TEXTURE_REPEAT) == texture.repeats, "%s surface %d: repeat" % [label, s])
		demand(material.texture_filter == (BaseMaterial3D.TEXTURE_FILTER_NEAREST if raw.crisp
			else BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC), "%s surface %d: filter" % [label, s])
		demand(material.cull_mode == (BaseMaterial3D.CULL_DISABLED if raw.two_sided else BaseMaterial3D.CULL_BACK),
			"%s surface %d: culling" % [label, s])
		if raw.alpha_mode == IpropModel.ALPHA_CUTOUT:
			demand(material.transparency == BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
				and bits(material.alpha_scissor_threshold) == bits(raw.alpha_cutoff), "%s surface %d: cutout" % [label, s])
		elif raw.alpha_mode == IpropModel.ALPHA_BLEND:
			demand(material.transparency == BaseMaterial3D.TRANSPARENCY_ALPHA, "%s surface %d: glass" % [label, s])
		else:
			demand(material.transparency == BaseMaterial3D.TRANSPARENCY_DISABLED, "%s surface %d: opaque" % [label, s])
		# Material maps (v1.2): each bound to its slot, read from red.
		var maps := {}
		for image in texture.images:
			maps[image.kind] = image.image
		for slot in [[IpropModel.IMAGE_ROUGHNESS, material.roughness_texture], [IpropModel.IMAGE_METALLIC, material.metallic_texture],
				[IpropModel.IMAGE_AO, material.ao_texture]]:
			var bound: Texture2D = slot[1]
			demand((bound != null) == maps.has(slot[0]), "%s surface %d: map %d bound" % [label, s, slot[0]])
			if bound != null:
				demand(level(bound) == maps[slot[0]].get_data(), "%s surface %d: map %d bytes" % [label, s, slot[0]])
		demand(material.ao_enabled == maps.has(IpropModel.IMAGE_AO), "%s surface %d: occlusion" % [label, s])
		demand(material.emission_enabled == (raw.emission > 0.0), "%s surface %d: emission" % [label, s])
		if raw.emission > 0.0:
			var glowing: bool = material.emission_texture == material.albedo_texture
			if maps.has(IpropModel.IMAGE_EMISSION):
				glowing = material.emission_texture != null and level(material.emission_texture) == maps[IpropModel.IMAGE_EMISSION].get_data()
			demand(bits(material.emission_energy_multiplier) == bits(raw.emission) and glowing
				and material.emission_operator == BaseMaterial3D.EMISSION_OP_MULTIPLY, "%s surface %d: glowing texture" % [label, s])

# A value written as `ipgen --uniform` takes it, as set_input takes it.
func input_value(model: IpropModel, name: String, text: String) -> Variant:
	for input in model.inputs:
		if input.name == name:
			if input.type == "color":
				return Color.html(text)
			return float(text) if input.type == "f32" else int(text)
	return null

func check_corpus() -> Dictionary:
	var files := {}
	for entry in CORPUS:
		var path := compile(entry.source, entry.name + ".iprop3d")
		files[entry.name] = path
		var model := IpropModel.new()
		demand(model.load_file(path) == OK, "load " + entry.name)
		for inputs in entry.inputs:
			demand(model.reset_inputs() == OK, entry.name + ": reset inputs")
			var uniforms := PackedStringArray()
			for name in inputs:
				demand(model.set_input(name, input_value(model, name, inputs[name])) == OK, "%s: set %s" % [entry.name, name])
				uniforms.append_array(["--uniform", "%s=%s" % [name, inputs[name]]])
			for seed in entry.seeds:
				var label := "%s seed %d %s" % [entry.name, seed, inputs]
				var args := PackedStringArray([path, "--seed", str(seed), "--dump", "-o", work.path_join("unused.glb")])
				args.append_array(uniforms)
				var native := run("ipgen", args)
				demand(native.code == 0, label + ": native ipgen")
				var expected: Variant = JSON.parse_string(native.text)
				demand(expected is Dictionary, label + ": native dump")
				if not (expected is Dictionary):
					continue
				var mesh := model.generate(seed)
				demand(mesh != null, label + ": generate")
				if mesh == null:
					continue
				var found := difference(model, expected)
				demand(found == "", "%s: %s differs from the native result" % [label, found])
				check_mesh(model, mesh, label)
				generations += 1
				vertices += model.vertex_count
	return files

func check_model(files: Dictionary) -> void:
	var model := IpropModel.new()
	demand(not model.is_loaded() and model.inputs.is_empty() and model.surfaces.is_empty(), "an empty model")
	demand(model.generate(1) == null and model.last_error.status == E_ARGUMENT, "generate before load")
	demand(model.set_input("wide", 1.0) == ERR_UNCONFIGURED and model.reset_inputs() == ERR_UNCONFIGURED, "inputs before load")
	demand(model.load_file(work.path_join("missing.iprop3d")) == ERR_FILE_NOT_FOUND, "a missing file")
	# A 2D program is no model, and a model no 2D program.
	var skin := compile("backends/capi3d/test/tile.ips", "tile.iprop")
	demand(model.load_file(skin) == ERR_INVALID_DATA and not model.is_loaded(), "a 2D program is not a model")
	var flat := IpropAsset.new()
	demand(flat.load_file(files.crate) == ERR_INVALID_DATA, "a model is not a 2D program")

	demand(model.load_file(files.crate) == OK and model.is_loaded(), "load the crate")
	demand(model.size == Vector3(12, 10, 8) and model.units_per_meter == 50.0 and model.fuel > 0, "what the crate declares")
	demand(model.skin_programs == [{"index": 0, "name": "tile", "width": 16, "height": 8}], "skin programs")
	var inputs: Array = model.inputs
	demand(inputs.size() == 4 and inputs[0] == {"index": 0, "name": "wide", "type": "f32", "default": 8.0, "value": 8.0}, "the f32 input")
	demand(inputs[1].type == "color" and inputs[1].default == Color8(0x33, 0x66, 0x99) and inputs[2].type == "u32" and inputs[2].default == 1,
		"the colour and u32 inputs")
	demand(model.vertex_count == 0 and model.textures.is_empty() and model.work.is_empty(), "nothing generated yet")

	# One generation: a mesh, and the generation's own numbers as properties.
	var mesh := model.generate(5)
	demand(mesh != null and mesh.get_surface_count() == 4 and model.vertex_count == 127 and model.triangle_count == 59, "the crate's mesh")
	demand(model.last_error.is_empty() and model.fuel_used > 0 and model.work.used > 0 and model.work.budget == 100000000, "counters")
	demand(model.bounds_min == Vector3(-4, 0, -4) and model.bounds_max == Vector3(4, 10, 4), "bounds in model units")
	var aabb := mesh.get_aabb()
	demand(aabb.position.is_equal_approx(Vector3(-0.08, 0, -0.08)) and aabb.size.is_equal_approx(Vector3(0.16, 0.2, 0.16)), "the mesh is in metres")
	var first: Dictionary = model.textures[0]
	demand(first.normal != null and first.normal == first.images[0].image and first.images[0].kind == IpropModel.IMAGE_NORMAL, "the normal image")
	# The fixed texture is what IpropAsset makes of the skin's own program.
	var tile := IpropAsset.new()
	demand(tile.load_file(skin) == OK and tile.set_input("tint", Color8(0x33, 0x66, 0x99)) == OK, "the skin program alone")
	var alone := tile.generate(77, 32, 16)
	demand(alone != null and alone.get_data() == first.image.get_data(), "a texture is the 2D generation of its skin program")
	demand(tile.normal_image != null and tile.normal_image.get_data() == first.normal.get_data(), "and so is its normal map")

	# A failed generation returns null, says why and keeps the previous one.
	var kept: Array = model.surfaces
	var kept_positions: PackedFloat32Array = kept[0].positions
	demand(model.set_input("wide", 0.0) == OK, "an input that breaks the box")
	demand(model.generate(5) == null, "a trap returns null")
	demand(model.last_error.status == E_TRAP and model.last_error.reason == REASON_NUMERIC_RANGE and model.last_error.stage == STAGE_EXECUTE
		and "box size must be positive" in model.last_error.message, "the trap's reason and message")
	demand(model.surfaces == kept and model.surfaces[0].positions == kept_positions and model.vertex_count == 127, "the previous generation stays")
	demand(model.set_input("wide", 8.0) == OK and model.set_input("rounding", 100.0) == OK, "an input that breaks the skin")
	demand(model.generate(5) == null and model.last_error.reason == REASON_NUMERIC_RANGE and model.last_error.message.begins_with("skin 'tile': "),
		"a failure inside a texture names its skin")
	demand(model.reset_inputs() == OK and model.generate(5) != null and model.last_error.is_empty(), "the model recovers")

	# Inputs are checked when they are set.
	demand(model.set_input("tall", 1.0) == ERR_INVALID_PARAMETER and model.set_input("wide", Color.RED) == ERR_INVALID_PARAMETER
		and model.set_input("extra", -2147483649) == ERR_INVALID_PARAMETER and model.set_input("stain", 3) == ERR_INVALID_PARAMETER,
		"rejected inputs")
	demand(model.set_input("extra", 0) == OK and model.generate(5) != null and model.vertex_count == 120 and model.surfaces.size() == 2,
		"an input changes the model")
	demand(model.inputs[2].value == 0 and model.inputs[2].default == 1, "the set value is reported")

	# Limits of the host cover the model and its textures.
	demand(model.generate(5, {"work_limit": 1}) == null and model.last_error.reason == REASON_WORK_EXHAUSTED, "the work limit")
	demand(model.generate(5, {"work_limit": 200}) == null and model.last_error.reason == REASON_WORK_EXHAUSTED
		and "skin 'tile'" in model.last_error.message, "the work limit inside a texture")
	var cancellation := IpropCancellation.new()
	demand(model.generate(5, {"cancellation": cancellation}) != null, "an idle cancellation")
	cancellation.cancel()
	demand(model.generate(5, {"cancellation": cancellation}) == null and model.last_error.reason == REASON_CANCELLED, "cancellation")
	demand(model.generate(5, {"speed": 1}) == null and model.last_error.status == E_ARGUMENT, "an unknown limit")
	demand(model.generate(-1) == null and model.generate(4294967296) == null and model.last_error.status == E_ARGUMENT, "a seed out of range")
	demand(model.generate(4294967295) != null, "the largest seed")

	# A new load forgets the generation; results handed out before keep their data.
	var image: Image = first.image
	var pixels := image.get_data()
	demand(model.load_file(files.empty) == OK and model.surfaces.is_empty() and model.vertex_count == 0, "a reload starts over")
	demand(image.get_data() == pixels and kept[0].positions == kept_positions, "earlier results own their data")
	var nothing := model.generate(3)
	demand(nothing != null and nothing.get_surface_count() == 0 and model.textures.is_empty() and model.bounds_max == Vector3.ZERO,
		"a model without triangles is an empty mesh")

func check_node(files: Dictionary) -> void:
	var node := IpropModelInstance3D.new()
	demand(node.mesh == null and node.seed == 0 and node.model == null, "an empty node")
	node.refresh()
	demand(node.mesh == null, "nothing to refresh")
	var model := IpropModel.new()
	node.model = model
	demand(node.mesh == null, "a model that is not loaded")
	demand(model.load_file(files.crate) == OK, "load for the node")
	demand(node.mesh != null and node.mesh.get_surface_count() == 4, "loading the model shows it")
	var before: PackedVector3Array = node.mesh.surface_get_arrays(0)[Mesh.ARRAY_VERTEX]
	node.seed = 6
	demand(node.mesh != null and node.mesh.surface_get_arrays(0)[Mesh.ARRAY_VERTEX] != before, "another seed, another shape")
	demand(model.set_input("extra", 0) == OK and node.mesh.get_surface_count() == 2, "an input refreshes the node")
	demand(model.set_input("wide", 0.0) == OK and node.mesh == null, "a failed generation leaves no mesh")
	demand(model.reset_inputs() == OK and node.mesh != null and node.mesh.get_surface_count() == 4, "and the node recovers")
	# Another model takes over; the first no longer refreshes the node.
	var other := IpropModel.new()
	demand(other.load_file(files.chair) == OK, "load the chair")
	node.model = other
	var chair := node.mesh
	demand(chair != null and chair.get_surface_count() >= 2, "the chair")
	demand(model.set_input("wide", 3.0) == OK and node.mesh == chair, "the replaced model is disconnected")
	node.model = null
	demand(node.mesh == null, "no model, no mesh")
	node.free()

# Equal texture keys share one GPU texture between meshes; a texture no mesh
# holds any more leaves the cache at the next generation.
# Two meshes of one seed, released when this function returns (GDScript
# temporaries live until then).
func shared_meshes(model: IpropModel) -> void:
	var first := model.generate(1)
	var second := model.generate(1)
	demand(first != null and second != null and first != second, "cache: two meshes")
	if first == null or second == null:
		return
	var a := first.surface_get_material(0) as StandardMaterial3D
	var b := second.surface_get_material(0) as StandardMaterial3D
	demand(a.albedo_texture == b.albedo_texture and a.roughness_texture == b.roughness_texture, "cache: one texture per key")
	demand(model.get_cached_texture_count() > 0, "cache: textures kept")

func check_texture_cache(files: Dictionary) -> void:
	var model := IpropModel.new()
	demand(model.load_file(files["materials"]) == OK, "cache: load")
	shared_meshes(model)
	var other := model.generate(2)
	demand(other != null, "cache: generate another seed")
	var kept := model.get_cached_texture_count()
	# Seed 2 keeps the net (its seed is a fork of the root's, so it changes
	# too) only through its own mesh: the first two meshes' textures are gone.
	var held := {}
	for s in other.get_surface_count():
		var material := other.surface_get_material(s) as StandardMaterial3D
		for texture in [material.albedo_texture, material.normal_texture, material.roughness_texture, material.metallic_texture,
				material.ao_texture, material.emission_texture]:
			if texture != null:
				held[texture] = true
	demand(kept == held.size(), "cache: only the live mesh's textures stay (%d kept, %d held)" % [kept, held.size()])
	model.clear_texture_cache()
	demand(model.get_cached_texture_count() == 0, "cache: cleared")

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3:
		printerr("usage: godot --headless --script models.gd -- <source-dir> <tools-dir> <work-dir>")
		quit(2)
		return
	source_dir = args[0]
	tools = args[1]
	work = args[2]
	DirAccess.make_dir_recursive_absolute(work)
	var files := check_corpus()
	check_model(files)
	check_node(files)
	check_texture_cache(files)
	if failures == 0:
		print("godot models: PASS (%d exact generations of %d vertices; meshes, materials, failures and the node)" % [generations, vertices])
	quit(1 if failures > 0 else 0)
