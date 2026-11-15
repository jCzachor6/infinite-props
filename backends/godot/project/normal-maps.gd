# Auxiliary images through IpropAsset and IpropSprite2D against the
# CLI-rendered shared fixtures.
#
# Usage (headless):
#   godot --headless --path . --script res://normal-maps.gd -- <binding-fixtures-dir>
extends SceneTree

var failures := 0

func demand(condition: bool, message: String) -> void:
	if not condition:
		printerr("FAIL ", message)
		failures += 1

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 1:
		printerr("usage: godot --headless --script normal-maps.gd -- <binding-fixtures-dir>")
		quit(2)
		return
	var fixtures: String = args[0]

	# An authored normal map: declared, published, byte-exact and owned.
	var authored := IpropAsset.new()
	demand(authored.load_file(fixtures.path_join("binding-normal.iprop")) == OK, "load binding-normal")
	var declared: Array = authored.declared_images
	demand(declared.size() == 1 and declared[0].index == 0 and declared[0].kind == IpropAsset.IMAGE_NORMAL and declared[0].name == "relief", "declared normal output")
	demand(authored.images.is_empty() and authored.normal_image == null, "no image before a generation")
	var image := authored.generate(5)
	demand(image != null and image.get_data() == FileAccess.get_file_as_bytes(fixtures.path_join("binding-normal.rgba")), "native RGBA oracle with a normal output")
	var expected := FileAccess.get_file_as_bytes(fixtures.path_join("binding-normal.normal.rgba"))
	var images: Array = authored.images
	var normal: Image = authored.normal_image
	demand(images.size() == 1 and images[0].image == normal, "one auxiliary image")
	demand(images[0].index == 0 and images[0].kind == IpropAsset.IMAGE_NORMAL and images[0].source == IpropAsset.SOURCE_AUTHORED and images[0].name == "relief", "authored tags")
	demand(normal != null and normal.get_size() == Vector2i(8, 4) and normal.get_format() == Image.FORMAT_RGBA8, "normal image layout")
	demand(normal.get_data() == expected, "native normal map oracle")
	# A later generation replaces the published image; the returned Image keeps its bytes.
	demand(authored.generate(6) != null and authored.normal_image.get_data() != expected, "seed-dependent direction")
	demand(normal.get_data() == expected, "returned images own their bytes")
	demand(authored.generate(5, 16, 8) != null and authored.normal_image.get_size() == Vector2i(16, 8), "scaled normal image")
	# An authored image wins over a derive request.
	authored.derive_normal = true
	authored.normal_depth = 9.0
	demand(authored.generate(5) != null and authored.images[0].source == IpropAsset.SOURCE_AUTHORED and authored.normal_image.get_data() == expected, "authored wins")
	# The bytes survive a GPU texture upload unchanged.
	var stored := ImageTexture.create_from_image(normal).get_image()
	demand(stored != null and stored.get_data() == expected, "GPU texture readback")

	# A sprite of this asset is lit: its texture carries the normal map.
	var lit := IpropSprite2D.new()
	var lit_asset := IpropAsset.new()
	demand(lit_asset.load_file(fixtures.path_join("binding-normal.iprop")) == OK, "load for a sprite")
	lit.seed = 5
	lit.asset = lit_asset
	var canvas := lit.texture as CanvasTexture
	demand(canvas != null and canvas.diffuse_texture != null and canvas.normal_texture != null, "sprite texture is a CanvasTexture")
	if canvas != null and canvas.normal_texture != null:
		demand(canvas.normal_texture.get_image().get_data() == expected, "sprite normal texture bytes")
		demand(canvas.diffuse_texture.get_image().get_data() == FileAccess.get_file_as_bytes(fixtures.path_join("binding-normal.rgba")), "sprite diffuse texture bytes")
	lit.free()

	# A derived normal map is opt-in, deterministic and leaves the image alone.
	var plain := IpropAsset.new()
	demand(plain.load_file(fixtures.path_join("bindings.iprop")) == OK, "load bindings")
	demand(plain.declared_images.is_empty(), "no declared image")
	var plain_image := plain.generate(5)
	var plain_work: int = plain.work.used
	demand(plain_image != null and plain.images.is_empty() and plain.normal_image == null, "derived images are opt-in")
	var unlit := IpropSprite2D.new()
	unlit.seed = 5
	unlit.asset = plain
	demand(unlit.texture is ImageTexture, "a sprite without a normal map keeps an ImageTexture")
	unlit.free()
	plain.derive_normal = true
	for request in [[0.0, 0.0, 0.0, "bindings-derived"], [2.0, 0.5, 0.25, "bindings-derived-custom"]]:
		plain.normal_depth = request[0]
		plain.normal_rounding = request[1]
		plain.normal_detail = request[2]
		var derived_image := plain.generate(5)
		var derived: Image = plain.normal_image
		demand(derived_image != null and derived != null, "derived generation " + request[3])
		if derived == null:
			continue
		demand(plain.images[0].kind == IpropAsset.IMAGE_NORMAL and plain.images[0].source == IpropAsset.SOURCE_DERIVED and plain.images[0].name == "normal", "derived tags")
		demand(derived.get_data() == FileAccess.get_file_as_bytes(fixtures.path_join(request[3] + ".normal.rgba")), "native derived normal oracle: " + request[3])
		demand(derived_image.get_data() == plain_image.get_data() and plain.work.used > plain_work, "main image unchanged, work spent")
	# The derivation runs under the generation's work budget.
	plain.normal_depth = 0.0
	plain.normal_rounding = 0.0
	plain.normal_detail = 0.0
	demand(plain.generate(5) != null, "derived defaults")
	var used: int = plain.work.used
	demand(plain.generate(5, 0, 0, {"work_limit": used}) != null and plain.work.remaining == 0, "exact work")
	demand(plain.generate(5, 0, 0, {"work_limit": used - 1}) == null and plain.last_error.reason == 3015 and plain.last_error.stage == 4, "work limit during derivation")
	# Out of range requests are argument errors and keep the previous images.
	var kept: Image = plain.normal_image
	for bad in [[-1.0, 0.0, 0.0], [INF, 0.0, 0.0], [0.0, 64.5, 0.0], [0.0, -0.5, 0.0], [0.0, 0.0, 1.5], [0.0, 0.0, NAN]]:
		plain.normal_depth = bad[0]
		plain.normal_rounding = bad[1]
		plain.normal_detail = bad[2]
		demand(plain.generate(5) == null and plain.last_error.status == 4 and plain.last_error.stage == 3, "invalid derive request")
		demand(plain.normal_image == kept, "a failed generation keeps the previous images")
	plain.derive_normal = false
	demand(plain.generate(5) != null and plain.normal_image == null, "recovery without a request")

	if failures == 0:
		print("Godot normal maps: PASS authored and derived images against the CLI, declarations, lit sprite texture, work limits and argument errors")
	quit(1 if failures > 0 else 0)
