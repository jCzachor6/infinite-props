# Many props through IpropModel (preview, M5): levels of detail against the
# native tools, textures a model still holds, generation on a thread of the
# WorkerThreadPool, the node's levels and a baked scene.
#
# Usage (headless):
#   godot --headless --path . --script res://scale.gd -- <source-dir> <tools-dir> <work-dir>
extends SceneTree

const Bake := preload("res://bake.gd")
const E_ARGUMENT := 4

var failures := 0
var source_dir := ""
var tools := ""
var work := ""

func demand(condition: bool, message: String) -> void:
	if not condition:
		printerr("FAIL ", message)
		failures += 1

func run(name: String, args: PackedStringArray) -> Dictionary:
	var output: Array = []
	var code := OS.execute(tools.path_join(name + (".exe" if OS.get_name() == "Windows" else "")), args, output, true)
	return {"code": code, "text": "".join(output)}

func bits(values: PackedFloat32Array) -> PackedInt64Array:
	var bytes := values.to_byte_array()
	var result := PackedInt64Array()
	for i in values.size():
		result.append(bytes.decode_u32(i * 4))
	return result

# What the extension generated against `ipgen --dump` at the same level.
func difference(model: IpropModel, expected: Dictionary) -> String:
	if model.vertex_count != int(expected.vertices) or model.triangle_count != int(expected.triangles):
		return "counts"
	if model.fuel_used != int(expected.fuel.spent) or model.work.used != int(expected.work.spent):
		return "fuel or work"
	if model.textures.size() != expected.textures.size() or model.surfaces.size() != expected.surfaces.size():
		return "texture or surface count"
	for t in model.textures.size():
		if model.textures[t].key != expected.textures[t].key or model.textures[t].repeats != expected.textures[t].repeats:
			return "texture %d" % t
	for s in model.surfaces.size():
		var actual := bits(model.surfaces[s].positions)
		var wanted: Array = expected.surfaces[s].positions
		if actual.size() != wanted.size():
			return "surface %d size" % s
		for i in actual.size():
			if actual[i] != int(wanted[i]):
				return "surface %d position %d" % [s, i]
	return ""

func native(path: String, seed: int, lod: int) -> Dictionary:
	var result := run("ipgen", [path, "--seed", str(seed), "--lod", str(lod), "--dump", "-o", work.path_join("unused.glb")])
	demand(result.code == 0, "native ipgen at level %d: %s" % [lod, result.text])
	var parsed: Variant = JSON.parse_string(result.text)
	return parsed if parsed is Dictionary else {}

# Levels of detail, each equal to the native result. The meshes live in this
# function only.
func check_levels(model: IpropModel, path: String) -> void:
	var meshes: Array = []
	var previous := 0
	for lod in 3:
		var mesh := model.generate(3, {}, lod)
		demand(mesh != null and model.lod == lod, "generate level %d" % lod)
		var found := difference(model, native(path, 3, lod))
		demand(found == "", "level %d: %s differs from the native result" % [lod, found])
		demand(lod == 0 or model.vertex_count < previous, "level %d has fewer vertices" % lod)
		# The meshes of the nearer levels are alive: their textures are not made again.
		demand(model.reused_texture_count == (0 if lod == 0 else model.textures.size()), "level %d reuses the textures" % lod)
		demand(model.textures[0].reused == (lod > 0) and model.textures[0].fuel > 0 and model.textures[0].image != null,
			"level %d: the texture's record and image" % lod)
		previous = model.vertex_count
		meshes.append(mesh)
	demand(model.generate(3, {}, 8) == null and model.last_error.status == E_ARGUMENT, "a level beyond 7")
	var near := meshes[0].surface_get_material(0) as StandardMaterial3D
	var far := meshes[2].surface_get_material(0) as StandardMaterial3D
	demand(near.albedo_texture == far.albedo_texture, "the levels share one GPU texture")

func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3:
		printerr("usage: godot --headless --script scale.gd -- <source-dir> <tools-dir> <work-dir>")
		quit(2)
		return
	source_dir = args[0]
	tools = args[1]
	work = args[2]
	DirAccess.make_dir_recursive_absolute(work)
	var path := work.path_join("lod.iprop3d")
	var compiled := run("ipc", [source_dir.path_join("examples/language/model-lod.ips"), "-o", path])
	demand(compiled.code == 0, "ipc: " + compiled.text)
	var model := IpropModel.new()
	demand(model.load_file(path) == OK, "load")

	check_levels(model, path)
	# The meshes are gone and nothing wears the textures any more: the next
	# generation makes them.
	demand(model.generate(3) != null and model.reused_texture_count == 0, "a texture no mesh wears is generated again")

	# ---- a generation on a thread ----
	var expected := native(path, 5, 1)
	var job := model.generate_async(5, {}, 1)
	demand(job > 0 and model.pending_jobs == 1, "a job is pending")
	demand(model.load_file(path) == ERR_BUSY, "no load while a job is pending")
	var answer: Array = await model.generated
	demand(answer[0] == job and answer[1] is ArrayMesh, "the signal carries the job and its mesh")
	demand(model.pending_jobs == 0 and model.lod == 1, "the job is done")
	var found := difference(model, expected)
	demand(found == "", "threaded: %s differs from the native result" % found)
	demand(model.generate_async(5, {}, 9) == -1 and model.last_error.status == E_ARGUMENT, "an invalid request starts no job")
	# Several at once, each answered once.
	var jobs := {}
	for seed in 6:
		jobs[model.generate_async(100 + seed, {}, seed % 3)] = true
	while not jobs.is_empty():
		var done: Array = await model.generated
		demand(jobs.has(done[0]) and done[1] != null, "job %d answers once with a mesh" % done[0])
		jobs.erase(done[0])
	demand(model.pending_jobs == 0, "every job is done")

	# ---- the node: a child a level, each in its range ----
	var node := IpropModelInstance3D.new()
	node.model = model
	node.seed = 3
	node.lod_distances = PackedFloat32Array([8.0])
	node.lod_levels = 3
	demand(node.get_child_count() >= 2 and node.mesh != null, "the node shows level 0 and has a child a level")
	var first := node.get_node_or_null("Lod1") as MeshInstance3D
	var second := node.get_node_or_null("Lod2") as MeshInstance3D
	demand(first != null and second != null and first.mesh != null and second.mesh != null, "the level children have meshes")
	if first != null and second != null:
		demand(node.visibility_range_end == 8.0 and first.visibility_range_begin == 8.0 and first.visibility_range_end == 20.0
			and second.visibility_range_begin == 20.0 and second.visibility_range_end == 0.0, "the ranges of the levels")
		demand(first.mesh.get_faces().size() < node.mesh.get_faces().size(), "a farther level has fewer triangles")
		demand(node.mesh.surface_get_material(0).albedo_texture == second.mesh.surface_get_material(0).albedo_texture,
			"the node's levels share their textures")
	node.threaded = true
	demand(node.mesh == null and model.pending_jobs == 3, "threaded: the meshes are on their way")
	while model.pending_jobs > 0:
		await model.generated
	first = node.get_node_or_null("Lod1") as MeshInstance3D
	demand(node.mesh != null and first != null and first.mesh != null, "threaded: the meshes arrived")
	node.lod_levels = 1
	demand(node.get_node_or_null("Lod1") == null and node.visibility_range_end == 0.0, "one level again")
	node.free()

	# ---- a baked scene: no generation left to do ----
	var baked := Bake.bake(model, 3, 3, PackedFloat32Array([8.0]))
	var scene_path := work.path_join("bollard.scn")
	demand(baked != null and ResourceSaver.save(baked, scene_path) == OK, "bake and save")
	var loaded := ResourceLoader.load(scene_path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE) as PackedScene
	demand(loaded != null, "the saved scene loads")
	if loaded != null:
		var root := loaded.instantiate()
		demand(root.get_child_count() == 3, "three levels in the scene")
		var counts: Array = []
		for level in root.get_child_count():
			var child := root.get_child(level) as MeshInstance3D
			var material := child.mesh.surface_get_material(0) as StandardMaterial3D
			demand(child.name == "Lod%d" % level and material != null and material.albedo_texture != null
				and material.albedo_texture.get_width() > 0, "level %d keeps its mesh, material and texture" % level)
			counts.append(child.mesh.get_faces().size())
		demand(counts[0] > counts[1] and counts[1] > counts[2], "the baked levels get coarser")
		var middle := root.get_child(1) as MeshInstance3D
		demand(middle.visibility_range_begin == 8.0 and middle.visibility_range_end == 20.0, "the baked ranges")
		root.free()
	demand(Bake.distance(0, 1, PackedFloat32Array()) == 0.0 and Bake.distance(1, 3, PackedFloat32Array()) == 25.0, "default distances")

	if failures == 0:
		print("godot scale: PASS (levels of detail, kept textures, threaded generation, the node's levels and a baked scene)")
	quit(1 if failures > 0 else 0)
