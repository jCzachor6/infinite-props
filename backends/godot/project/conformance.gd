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
	# size overrides are covered by the C API; this adapter exposes the
	# seed/variant/archetype parameters, the asset size and the root inputs.
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

	# Root inputs: reflection, typed setters (negative ints keep i32 bits),
	# rejected values that change nothing, sprite refresh, reset and reload.
	var unloaded := IpropAsset.new()
	if unloaded.set_input("base", Color8(0, 0, 0)) != ERR_UNCONFIGURED \
			or unloaded.reset_inputs() != ERR_UNCONFIGURED or not unloaded.inputs.is_empty():
		failures += 1
		printerr("FAIL inputs before load")
	var tinted := IpropAsset.new()
	if tinted.load_file(args[0] + "/uniforms.iprop") != OK:
		failures += 1
		printerr("FAIL uniforms fixture load")
	else:
		var defaults := [Color8(0x33, 0x55, 0xaa), 1.0, 1]
		var declared: Array = tinted.inputs
		var shape := declared.map(func(input): return [input["index"], input["name"], input["type"]])
		if shape != [[0, "base", "color"], [1, "fx", "f32"], [2, "ux", "u32"]] \
				or declared.map(func(input): return input["default"]) != defaults \
				or declared.map(func(input): return input["value"]) != defaults:
			failures += 1
			printerr("FAIL input reflection ", declared)
		var plain := tinted.generate(0, 0, 0)
		var sprite_view := IpropSprite2D.new()
		sprite_view.asset = tinted
		if tinted.set_input("base", Color8(255, 0, 0)) != OK or tinted.set_input("fx", 5.5) != OK \
				or tinted.set_input("ux", 3) != OK:
			failures += 1
			printerr("FAIL set_input")
		var set_values := [Color8(255, 0, 0), 5.5, 3]
		for rejected in [["missing", 1], ["ux", 1.5], ["ux", 4294967296], ["ux", -2147483649],
				["base", 1], ["fx", "x"]]:
			if tinted.set_input(rejected[0], rejected[1]) != ERR_INVALID_PARAMETER:
				failures += 1
				printerr("FAIL accepted input ", rejected)
		if tinted.inputs.map(func(input): return input["value"]) != set_values:
			failures += 1
			printerr("FAIL input values after rejected sets")
		var custom := tinted.generate(0, 0, 0)
		if plain == null or custom == null or custom.get_pixel(0, 0) != Color8(255, 0, 0) \
				or custom.get_pixel(1, 1) == plain.get_pixel(1, 1) \
				or custom.get_pixel(2, 2) == plain.get_pixel(2, 2):
			failures += 1
			printerr("FAIL generation with inputs")
		if sprite_view.texture == null \
				or sprite_view.texture.get_image().get_data() != custom.get_data():
			failures += 1
			printerr("FAIL sprite refresh after set_input")
		if tinted.set_input("ux", -2) != OK or tinted.inputs[2]["value"] != 4294967294:
			failures += 1
			printerr("FAIL negative u32 input bits")
		if tinted.reset_inputs() != OK or tinted.generate(0, 0, 0).get_data() != plain.get_data():
			failures += 1
			printerr("FAIL reset_inputs")
		tinted.set_input("fx", 2)
		if tinted.load_file(args[0] + "/uniforms.iprop") != OK \
				or tinted.inputs.map(func(input): return input["value"]) != defaults:
			failures += 1
			printerr("FAIL reload keeps old inputs")
		if tinted.load_file(args[0] + "/shapes.iprop") != OK or tinted.set_input("base", Color8(0, 0, 0)) \
				!= ERR_INVALID_PARAMETER:
			failures += 1
			printerr("FAIL inputs follow the loaded program")
		sprite_view.free()

	# Empty/bad generations clear the sprite. Failed generations/reloads preserve the
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
	if sprite.texture != null or lifecycle.outputs != old_outputs:
		failures += 1
		printerr("FAIL failed generation texture/map publication")
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
	if lifecycle.outputs != old_outputs:
		failures += 1
		printerr("FAIL rejected arguments replaced published outputs")
	sprite.free()

	# Existing helper/part fixture: every D4 transform, owner gap, unset
	# singular and empty list. Typed reads keep the already transformed leaves,
	# in root coordinates: the D4 image plus the part's blit origin {3, 4}.
	var typed := IpropAsset.new()
	if typed.load_file(args[0] + "/output-values.iprop") != OK:
		failures += 1
	else:
		var points := [Vector2(1,2), Vector2(2,7), Vector2(7,6), Vector2(6,1),
			Vector2(7,2), Vector2(1,6), Vector2(2,1), Vector2(6,7)]
		var rects := [PackedFloat32Array([1,2,3,4]), PackedFloat32Array([2,4,4,3]),
			PackedFloat32Array([4,2,3,4]), PackedFloat32Array([2,1,4,3]),
			PackedFloat32Array([4,2,3,4]), PackedFloat32Array([1,2,3,4]),
			PackedFloat32Array([2,1,4,3]), PackedFloat32Array([2,4,4,3])]
		var names := ["anchor", "collider", "zone", "anchors", "colliders", "zones"]
		var transforms := [0,1,2,3,4,7,5,6]
		var saved_image: Image
		var saved_bytes: PackedByteArray
		var saved_slots: Array
		for v in range(8):
			var image := typed.generate(26, v, 0)
			var slots: Array = typed.output_slots
			if image == null or slots.size() != 18 or typed.part_outputs.keys() != [1,3]:
				failures += 1
				printerr("FAIL typed output owner gaps/counts")
				continue
			for i in range(slots.size()):
				var owner := 0 if i < 6 else (1 if i < 12 else 3)
				var local := i % 6
				var count := (int(v != 0) if local == 0 else (1 if local < 3 else 2)) if owner == 0 \
					else ((1 if local < 3 else 0) if owner == 1 else (2 if local == 0 else 0))
				var is_list := local >= 3 if owner < 3 else (local == 0 or local >= 4)
				if slots[i]["index"] != i or slots[i]["owner"] != owner \
						or slots[i]["name"] != names[local] or slots[i]["list"] != is_list \
						or slots[i]["kind"] != local % 3 or slots[i]["values"].size() != count * [2,4,3][local % 3]:
					failures += 1
			var point: Vector2 = points[transforms[v]] + Vector2(3, 4)
			var rect: PackedFloat32Array = rects[transforms[v]].duplicate()
			rect[0] += 3
			rect[1] += 4
			if slots[6]["values"] != PackedFloat32Array([point.x, point.y]) \
					or slots[7]["values"] != rect \
					or slots[8]["values"] != PackedFloat32Array([point.x, point.y, 3]) \
					or slots[0]["values"].is_empty() != (v == 0) \
					or not slots[9]["values"].is_empty() or not slots[13]["values"].is_empty() \
					or slots[12]["values"] != PackedFloat32Array([9,10,11,12]) \
					or slots[3]["values"] != PackedFloat32Array([1,2,11,12]):
				failures += 1
				printerr("FAIL typed D4/empty/list/helper outputs v", v)
			if v == 0:
				saved_image = image
				saved_bytes = image.get_data()
				saved_slots = slots
		# Reload frees the previous program; prior engine objects remain owned.
		if typed.load_file(args[0] + "/portrait.iprop") != OK \
				or typed.generate(0,0,0) == null or saved_image.get_data() != saved_bytes \
				or saved_slots[0]["values"].size() != 0 or saved_slots[3]["values"].size() != 4:
			failures += 1
			printerr("FAIL engine image/output lifetime after reload/new generation")

	# Check the actual engine String for embedded NUL; a length-taking UTF-8
	# conversion alone is not evidence. Float arrays must retain exact bits.
	var raw := IpropAsset.new()
	var expected_bits := [0x80000000,1,0x7fc12345,0xff800000,0x7f800000,
		0xffc12345,0x80000001,0x3f800000,0x80000000]
	for fixture in ["backend-raw-values.iprop", "backend-empty-values.iprop"]:
		if raw.load_file(args[0] + "/" + fixture) != OK or raw.generate(0,0,0) == null:
			failures += 1
			printerr("FAIL raw output fixture load/generate")
			continue
		var slots: Array = raw.output_slots
		var exact_name: String = slots[0]["name"]
		var zeros_name: String = slots[2]["name"]
		if slots.size() != 6 or exact_name.length() != 9 or exact_name.unicode_at(0) != 112 \
				or exact_name.unicode_at(1) != 322 or exact_name.unicode_at(2) != 0 \
				or exact_name.to_utf8_buffer() != PackedByteArray([112,197,130,0,115,117,102,102,105,120]) \
				or exact_name.substr(3) != "suffix" or slots[1]["name"] != "" \
				or not raw.outputs.has(exact_name) or raw.part_outputs.keys() != [2] \
				or raw.outputs.has("pł") or zeros_name.length() != 4 \
				or zeros_name.unicode_at(0) != 0 or zeros_name.unicode_at(1) != 99 \
				or zeros_name.unicode_at(2) != 0 or zeros_name.unicode_at(3) != 0 \
				or zeros_name.to_utf8_buffer() != PackedByteArray([0,99,0,0]) \
				or raw.outputs[zeros_name]["index"] != 2 or raw.outputs[""]["index"] != 1:
			failures += 1
			printerr("FAIL exact UTF-8/NUL/empty names and owner gap")
		var component := 0
		for i in range(slots.size()):
			var values: PackedFloat32Array = slots[i]["values"]
			if fixture == "backend-raw-values.iprop" and i < 3:
				var value_bits := values.to_byte_array()
				for j in range(values.size()):
					if value_bits.decode_u32(j * 4) != expected_bits[component]:
						failures += 1
						printerr("FAIL exact raw output bits at ", component)
					component += 1
			elif not values.is_empty():
				failures += 1
				printerr("FAIL missing singular slot unexpectedly populated")

	if failures == 0:
		print("godot conformance: PASS (", entries.size(), " corpus entries + the outputs)")
	else:
		printerr("godot conformance: ", failures, " failure(s)")
	quit(0 if failures == 0 else 1)
