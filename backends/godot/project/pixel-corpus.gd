extends SceneTree

var reader := StreamPeerBuffer.new()
var failed := false

func word() -> int:
	assert(reader.get_available_bytes() >= 4, "Truncated corpus")
	return reader.get_u32()

func blob() -> PackedByteArray:
	var size := word()
	assert(size <= reader.get_available_bytes(), "Truncated corpus blob")
	return reader.get_data(size)[1]

func demand(condition: bool, message: String) -> void:
	if not condition:
		printerr(message)
		failed = true

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 1:
		printerr("usage: godot --script pixel-corpus.gd -- <pixel-corpus-directory>")
		quit(2)
		return
	var data := FileAccess.get_file_as_bytes(args[0].path_join("pixel-corpus.bin"))
	var manifest = JSON.parse_string(FileAccess.get_file_as_string(args[0].path_join("pixel-corpus.json")))
	var digest := HashingContext.new()
	digest.start(HashingContext.HASH_SHA256)
	digest.update(data)
	var hash_text := digest.finish().hex_encode()
	demand(manifest.sha256 == hash_text, "Corpus SHA-256 mismatch")
	reader.data_array = data
	reader.big_endian = false
	demand(reader.get_data(8)[1].get_string_from_ascii() == "IPXCRP02", "Corpus version")
	var programs: Array[String] = []
	var directory := "user://pixel-corpus/" + hash_text
	DirAccess.make_dir_recursive_absolute(directory)
	for i in range(word()):
		var file := directory.path_join(str(i) + ".iprop")
		FileAccess.open(file, FileAccess.WRITE).store_buffer(blob())
		programs.append(file)
	var count := word()
	demand(count > 0 and count == int(manifest.cases), "Incomplete corpus")
	var auxiliary_images := 0
	for index in range(count):
		var asset := IpropAsset.new()
		demand(asset.load_file(programs[word()]) == OK, "Program load")
		var seed := word()
		var width := word()
		var height := word()
		var declarations: Array = asset.inputs
		for j in range(word()):
			var name := blob().get_string_from_utf8()
			var tag := word()
			var bits := word()
			var integer := word()
			var value: Variant
			if tag == 0:
				value = Color8(bits & 255, (bits >> 8) & 255, (bits >> 16) & 255, (bits >> 24) & 255)
			else:
				var is_float := false
				for declaration in declarations:
					if declaration.name == name:
						is_float = declaration.type == "f32"
				var packed := PackedByteArray()
				packed.resize(4)
				packed.encode_u32(0, bits)
				value = packed.decode_float(0) if is_float else integer
			demand(asset.set_input(name, value) == OK, "Input override")
		# The derived-normal request: one flag and three binary32 values.
		asset.derive_normal = word() != 0
		var request: PackedByteArray = reader.get_data(12)[1]
		asset.normal_depth = request.decode_float(0)
		asset.normal_rounding = request.decode_float(4)
		asset.normal_detail = request.decode_float(8)
		var expected_width := word()
		var expected_height := word()
		var expected := blob()
		var image := asset.generate(seed, width, height, {"work_limit": 1000000000})
		var label := "Godot corpus case %d seed %d" % [index, seed]
		demand(image != null and image.get_size() == Vector2i(expected_width, expected_height), label + ": size")
		demand(image.get_data() == expected, label + ": RGBA")
		var outputs: Array = asset.output_slots
		demand(outputs.size() == word(), label + ": outputs")
		for output in outputs:
			demand(output.name.to_utf8_buffer() == blob(), label + ": name")
			word() # Reserved; WASM does not expose owner IDs.
			var kind := word()
			var list_flag := word() != 0
			var elements := word()
			var leaves := word()
			var values: PackedFloat32Array = output["values"]
			demand(output.kind == kind and output.list == list_flag and output.count == elements and values.size() == leaves and leaves == elements * [2, 4, 3, 0][kind], label + ": slot")
			var packed := values.to_byte_array()
			for leaf in range(leaves):
				demand(packed.decode_u32(leaf * 4) == word(), label + ": f32 bits")
		if failed:
			quit(1)
			return
		var texture := ImageTexture.create_from_image(image)
		var readback := texture.get_image()
		demand(readback != null and readback.get_data() == expected, label + ": GPU texture readback")
		# Auxiliary images are part of the gate: every byte, name and tag.
		var images: Array = asset.images
		demand(images.size() == word(), label + ": auxiliary image count")
		for auxiliary in images:
			var kind := word()
			var source := word()
			var auxiliary_name := blob()
			var auxiliary_width := word()
			var auxiliary_height := word()
			var auxiliary_rgba := blob()
			var picture: Image = auxiliary.image
			demand(auxiliary.kind == kind and auxiliary.source == source and auxiliary.name.to_utf8_buffer() == auxiliary_name, label + ": auxiliary image tags")
			demand(picture.get_size() == Vector2i(auxiliary_width, auxiliary_height) and picture.get_data() == auxiliary_rgba, label + ": auxiliary RGBA")
			var stored := ImageTexture.create_from_image(picture).get_image()
			demand(stored != null and stored.get_data() == auxiliary_rgba, label + ": auxiliary GPU texture readback")
			auxiliary_images += 1
		if failed:
			quit(1)
			return
	demand(reader.get_available_bytes() == 0, "Unread corpus cases")
	demand(auxiliary_images > 0, "Corpus must exercise auxiliary images")
	print("Godot + Image + GPU texture: %d pixel-exact cases with %d auxiliary images; SHA-256 %s" % [count, auxiliary_images, hash_text])
	quit(1 if failed else 0)
