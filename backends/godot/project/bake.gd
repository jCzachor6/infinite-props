# bake.gd — a generated model as a saved scene (preview, M5): the meshes of
# its levels of detail with their materials and textures, each within its
# range of distances. A baked scene needs no generation and no extension to
# show; it is what a game ships when a prop never changes.
#
# Usage (headless):
#   godot --headless --path . --script res://bake.gd -- <model.iprop3d> <out.scn> [seed] [levels] [d0,d1,...]
# From a script:
#   const Bake := preload("res://bake.gd")
#   ResourceSaver.save(Bake.bake(model, 7, 3), "res://props/chair-7.scn")
extends SceneTree

# The distance at which a level hands over to the next: the caller's where it
# gives one, else 10 m and 2.5 times more a level, as IpropModelInstance3D.
static func distance(level: int, levels: int, distances: PackedFloat32Array) -> float:
	if level < 0 or level >= levels - 1:
		return 0.0
	var result := 10.0
	for i in level + 1:
		var given := distances[i] if i < distances.size() else 0.0
		result = given if given > 0.0 else (10.0 if i == 0 else result * 2.5)
	return result

# A scene of one Node3D with a MeshInstance3D for each level, or null when a
# generation fails (the model's last_error tells why).
static func bake(model: IpropModel, seed: int, levels: int = 1,
		distances: PackedFloat32Array = PackedFloat32Array()) -> PackedScene:
	var root := Node3D.new()
	root.name = "Model"
	for level in levels:
		var mesh := model.generate(seed, {}, level)
		if mesh == null:
			root.free()
			return null
		var node := MeshInstance3D.new()
		node.name = "Lod%d" % level
		node.mesh = mesh
		node.visibility_range_begin = distance(level - 1, levels, distances)
		node.visibility_range_end = distance(level, levels, distances)
		root.add_child(node)
		node.owner = root
	var scene := PackedScene.new()
	var packed := scene.pack(root)
	root.free()
	return scene if packed == OK else null

func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() < 2 or args.size() > 5:
		printerr("usage: godot --headless --script bake.gd -- <model.iprop3d> <out.scn> [seed] [levels] [d0,d1,...]")
		quit(2)
		return
	var model := IpropModel.new()
	if model.load_file(args[0]) != OK:
		quit(1)
		return
	var levels := clampi(int(args[3]) if args.size() > 3 else 1, 1, 8)
	var distances := PackedFloat32Array()
	if args.size() > 4:
		for text in args[4].split(","):
			distances.append(float(text))
	var scene := bake(model, int(args[2]) if args.size() > 2 else 0, levels, distances)
	var saved := scene != null and ResourceSaver.save(scene, args[1]) == OK
	# Nothing of the model or the scene is left when the tree quits.
	scene = null
	model = null
	if not saved:
		printerr("bake: cannot write ", args[1])
		quit(1)
		return
	print("baked ", args[1], " (", levels, " levels)")
	quit(0)
