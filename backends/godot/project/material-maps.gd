# Material maps (spec material maps v1.2) through IpropAsset against the
# CLI-rendered shared fixture.
#
# Usage (headless):
#   godot --headless --path . --script res://material-maps.gd -- <binding-fixtures-dir>
extends SceneTree

var failures := 0

func demand(condition: bool, message: String) -> void:
	if not condition:
		printerr("FAIL ", message)
		failures += 1

func grey(image: Image) -> bool:
	var data := image.get_data()
	for i in range(0, data.size(), 4):
		if data[i] != data[i + 1] or data[i] != data[i + 2] or data[i + 3] != 255:
			return false
	return true

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 1:
		printerr("usage: godot --headless --script material-maps.gd -- <binding-fixtures-dir>")
		quit(2)
		return
	var fixtures: String = args[0]
	var kinds := [IpropAsset.IMAGE_NORMAL, IpropAsset.IMAGE_ROUGHNESS, IpropAsset.IMAGE_METALLIC, IpropAsset.IMAGE_AO, IpropAsset.IMAGE_EMISSION]
	var names := ["relief", "gloss", "metal", "shade", "glow"]
	var files := ["normal", "roughness", "metallic", "ao", "emission"]
	demand(kinds == [1, 2, 3, 4, 5], "kind constants")

	var asset := IpropAsset.new()
	demand(asset.load_file(fixtures.path_join("binding-maps.iprop")) == OK, "load binding-maps")
	# Declared out of order in the source; listed by kind.
	var declared: Array = asset.declared_images
	demand(declared.size() == 5, "five declared images")
	for i in range(min(declared.size(), 5)):
		demand(declared[i].index == i and declared[i].kind == kinds[i] and declared[i].name == names[i], "declared image %d" % i)
	var image := asset.generate(5)
	demand(image != null and image.get_data() == FileAccess.get_file_as_bytes(fixtures.path_join("binding-maps.rgba")), "native RGBA oracle")
	var images: Array = asset.images
	demand(images.size() == 5, "five published images")
	for i in range(min(images.size(), 5)):
		var entry: Dictionary = images[i]
		demand(entry.index == i and entry.kind == kinds[i] and entry.source == IpropAsset.SOURCE_AUTHORED and entry.name == names[i], "tags of image %d" % i)
		var picture: Image = entry.image
		demand(picture.get_size() == Vector2i(8, 4) and picture.get_format() == Image.FORMAT_RGBA8, "layout of image %d" % i)
		demand(picture.get_data() == FileAccess.get_file_as_bytes(fixtures.path_join("binding-maps." + files[i] + ".rgba")), "native oracle of the " + files[i] + " map")
		demand(asset.get_image(kinds[i]) == picture, "get_image of kind %d" % kinds[i])
	for kind in [IpropAsset.IMAGE_ROUGHNESS, IpropAsset.IMAGE_METALLIC, IpropAsset.IMAGE_AO]:
		demand(asset.get_image(kind) != null and grey(asset.get_image(kind)), "a scalar map is grey (kind %d)" % kind)
	demand(asset.normal_image == asset.get_image(IpropAsset.IMAGE_NORMAL), "the normal map comes first")
	# The seed moves one roughness pixel and nothing else.
	var roughness := asset.get_image(IpropAsset.IMAGE_ROUGHNESS).get_data()
	var emission := asset.get_image(IpropAsset.IMAGE_EMISSION).get_data()
	demand(asset.generate(6) != null and asset.get_image(IpropAsset.IMAGE_ROUGHNESS).get_data() != roughness, "seed-dependent roughness")
	demand(asset.get_image(IpropAsset.IMAGE_EMISSION).get_data() == emission, "seed-independent emission")
	# The bytes survive a GPU texture upload unchanged.
	var stored := ImageTexture.create_from_image(asset.get_image(IpropAsset.IMAGE_EMISSION)).get_image()
	demand(stored != null and stored.get_data() == emission, "GPU texture readback")

	# A program without maps publishes none.
	var plain := IpropAsset.new()
	demand(plain.load_file(fixtures.path_join("bindings.iprop")) == OK, "load bindings")
	demand(plain.generate(5) != null and plain.get_image(IpropAsset.IMAGE_ROUGHNESS) == null, "no material map")

	if failures == 0:
		print("godot material maps: PASS")
	quit(1 if failures > 0 else 0)
