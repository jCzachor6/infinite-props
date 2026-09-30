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

	if failures == 0:
		print("godot conformance: PASS (", entries.size(), " corpus entries + the outputs)")
	else:
		printerr("godot conformance: ", failures, " failure(s)")
	quit(0 if failures == 0 else 1)
