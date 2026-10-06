# The Godot conformance (task 8.2/8.3): the golden corpus driven through
# the IpropAsset resource — generated Image pixels compared byte-for-byte
# against the committed golden PNGs, plus the pinned structured outputs.
#
# Usage (headless):
#   godot --headless --path . --script res://conformance.gd -- <iprop-dir> <png-dir>
extends SceneTree


func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 2:
		printerr("usage: godot --headless --script conformance.gd -- <iprop-dir> <png-dir>")
		quit(2)
		return

	var failures := 0
	var entries := [
		["portrait.iprop", "portrait-seed0.png", 0],
		["portrait.iprop", "portrait-seed1.png", 1],
		["portrait.iprop", "portrait-seed2.png", 2],
		["portrait.iprop", "portrait-seed7.png", 7],
		["portrait.iprop", "portrait-seed26.png", 26],
		["shapes.iprop", "shapes.png", 0],
		["noise.iprop", "noise.png", 0],
		["parts.iprop", "parts.png", 0],
	]

	for entry in entries:
		var asset := IpropAsset.new()
		var err := asset.load_file(args[0] + "/" + entry[0])
		if err != OK:
			failures += 1
			printerr("FAIL load ", entry[0])
			continue
		var image := asset.generate(entry[2], 0, 0)
		if image == null:
			failures += 1
			printerr("FAIL generate ", entry[0])
			continue
		var golden := Image.new()
		var golden_bytes := FileAccess.get_file_as_bytes(args[1] + "/" + entry[1])
		if golden.load_png_from_buffer(golden_bytes) != OK:
			failures += 1
			printerr("FAIL golden load ", entry[1])
			continue
		if image.get_size() != golden.get_size() or image.get_data() != golden.get_data():
			failures += 1
			printerr("FAIL pixels ", entry[0])
		else:
			print("PASS ", entry[0], " seed ", entry[2])

	# The pinned structured outputs (the portrait's pupilCenter and the
	# parts' center/spot).
	var portrait := IpropAsset.new()
	if portrait.load_file(args[0] + "/portrait.iprop") == OK:
		portrait.generate(0, 0, 0)
		var outputs: Dictionary = portrait.outputs
		if outputs.has("pupilCenter"):
			var slot: Dictionary = outputs["pupilCenter"]
			var values: PackedFloat32Array = slot["values"]
			if slot["kind"] == 0 and values.size() == 2 \
					and values[0] == 27.0 and values[1] == 28.0:
				print("PASS pupilCenter output")
			else:
				failures += 1
				printerr("FAIL pupilCenter output")
		else:
			failures += 1
			printerr("FAIL missing pupilCenter output")
	else:
		failures += 1
		printerr("FAIL portrait reload for the outputs")

	# The PNG export round-trip (the demo's S-key path).
	var shapes := IpropAsset.new()
	if shapes.load_file(args[0] + "/shapes.iprop") == OK:
		var exported := shapes.generate(0, 0, 0)
		exported.save_png("user://roundtrip.png")
		var reloaded := Image.new()
		if reloaded.load("user://roundtrip.png") != OK \
				or reloaded.get_data() != exported.get_data():
			failures += 1
			printerr("FAIL png round-trip")
		else:
			print("PASS png round-trip")
	else:
		failures += 1
		printerr("FAIL shapes reload for the round-trip")

	var owners := IpropAsset.new()
	if owners.load_file(args[0] + "/output-owners.iprop") != OK or owners.generate(0, 0, 0) == null:
		failures += 1
		printerr("FAIL owner fixture load/generate")
	else:
		var slots: Array = owners.output_slots
		if slots.size() != 9 or owners.outputs.size() != 3 or owners.part_outputs.size() != 2:
			failures += 1
			printerr("FAIL owner output counts")
		for i in range(slots.size()):
			var expected_owner := i / 3
			var expected_value := 4.0 if i < 3 else (1.0 if i < 6 else 2.0)
			if slots[i]["owner"] != expected_owner or slots[i]["values"][0] != expected_value:
				failures += 1
				printerr("FAIL ordered owner slot ", i)
		for owner in [1, 2]:
			for name in ["anchor", "collider", "zone"]:
				if owners.part_outputs[owner][name]["values"][0] != float(owner):
					failures += 1
		if owners.outputs["anchor"]["values"][0] != 4.0:
			failures += 1
			printerr("FAIL root output overwritten")

	# Current compiler source, including a list with zero values. Native
	# size/uniform overrides are covered by the C API; this adapter exposes
	# the seed/variant/archetype parameters and default asset size/inputs.
	var language := IpropAsset.new()
	if language.load_file(args[0] + "/language.iprop") != OK:
		failures += 1
		printerr("FAIL language source fixture load")
	else:
		var colors := [Color8(0x11, 0x22, 0x33), Color8(0x44, 0x55, 0x66), Color8(0x77, 0x88, 0x99)]
		for v in range(3):
			for a in range(2):
				var image := language.generate(26, v, a)
				if image == null:
					failures += 1
					continue
				if image.get_size() != Vector2i(8, 8) or image.get_pixel(0, 0) != Color8(0x33, 0x55, 0xaa) \
						or image.get_pixel(a + 1, 1) != colors[v]:
					failures += 1
					printerr("FAIL language pixels v", v, " a", a)
				if language.outputs["numeric"]["values"] != PackedFloat32Array([v + 1, a + 0.25]) \
						or language.outputs["path"]["values"] != PackedFloat32Array([0, a, 2, a]) \
						or not language.outputs["empty"]["values"].is_empty() \
						or not language.outputs["empty"]["list"]:
					failures += 1
					printerr("FAIL language output values")
				var math_bits: PackedByteArray = language.outputs["math"]["values"].to_byte_array()
				if math_bits.decode_u32(0) != 0x3f8db70d or math_bits.decode_u32(4) != 0x3f800000:
					failures += 1
					printerr("FAIL language math bits")
	var plant := IpropAsset.new()
	if plant.load_file(args[0] + "/plant.iprop") != OK or plant.generate(26, 0, 0) == null:
		failures += 1
		printerr("FAIL compiled plant source")

	# Empty/bad generations clear the sprite. Failed reloads preserve the
	# complete prior asset; successful reloads notify existing sprite users.
	var lifecycle := IpropAsset.new()
	var sprite := IpropSprite2D.new()
	sprite.asset = lifecycle
	if sprite.texture != null:
		failures += 1
	if lifecycle.load_file(args[0] + "/shapes.iprop") != OK or sprite.texture == null:
		failures += 1
	var old_size: Vector2i = lifecycle.size
	var old_metadata := [lifecycle.size, lifecycle.origin, lifecycle.variants,
		lifecycle.archetypes, lifecycle.fuel]
	var old_texture := sprite.texture
	var old_outputs: Dictionary = lifecycle.outputs.duplicate(true)
	if lifecycle.load_file(args[0] + "/missing.iprop") != ERR_FILE_NOT_FOUND \
			or not lifecycle.is_loaded() or lifecycle.size != old_size or sprite.texture == null:
		failures += 1
		printerr("FAIL transactional asset reload")
	# A failed decode also preserves the loaded handle, queried metadata and
	# previously published generation, without notifying sprite users.
	var invalid_path := "user://iprop-conformance-invalid.iprop"
	var invalid_file := FileAccess.open(invalid_path, FileAccess.WRITE)
	if invalid_file == null:
		failures += 1
	else:
		invalid_file.store_buffer(PackedByteArray([1, 2, 3, 4]))
		invalid_file.close()
		if lifecycle.load_file(invalid_path) != ERR_INVALID_DATA \
				or not lifecycle.is_loaded() or sprite.texture != old_texture \
				or lifecycle.outputs != old_outputs \
				or [lifecycle.size, lifecycle.origin, lifecycle.variants,
					lifecycle.archetypes, lifecycle.fuel] != old_metadata:
			failures += 1
			printerr("FAIL invalid binary reload replaced loaded asset")
		DirAccess.remove_absolute(invalid_path)
	sprite.variant = 99
	if sprite.texture != null or not lifecycle.outputs.is_empty():
		failures += 1
		printerr("FAIL failed generation kept stale texture/outputs")
	sprite.variant = 0
	if sprite.texture == null:
		failures += 1
	sprite.asset = null
	if sprite.texture != null:
		failures += 1
		printerr("FAIL null asset kept stale texture")
	if lifecycle.generate(4294967296, 0, 0) != null or lifecycle.generate(0, -1, 0) != null:
		failures += 1
		printerr("FAIL Godot integer input truncation")
	sprite.free()

	if failures == 0:
		print("godot conformance: PASS (", entries.size(), " corpus entries + the outputs)")
	else:
		printerr("godot conformance: ", failures, " failure(s)")
	quit(0 if failures == 0 else 1)
