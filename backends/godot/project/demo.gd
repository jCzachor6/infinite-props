# The demo (task 8.3): an IpropSprite2D over the plant, cycled with the
# arrow keys (seeds left/right, variants up/down).
extends Node2D

var sprite: IpropSprite2D
var asset: IpropAsset


func _ready() -> void:
	asset = IpropAsset.new()
	var err := asset.load_file("res://assets/plant.iprop")
	if err != OK:
		push_error("cannot load the plant")
		return
	sprite = IpropSprite2D.new()
	sprite.asset = asset
	add_child(sprite)


func _unhandled_input(event: InputEvent) -> void:
	if sprite == null:
		return
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode == KEY_LEFT:
			sprite.seed = sprite.seed - 1
		elif event.keycode == KEY_RIGHT:
			sprite.seed = sprite.seed + 1
		elif event.keycode == KEY_UP:
			sprite.variant = (sprite.variant + asset.variants - 1) % asset.variants
		elif event.keycode == KEY_DOWN:
			sprite.variant = (sprite.variant + 1) % asset.variants
		elif event.keycode == KEY_S:
			var image := asset.generate(sprite.seed, sprite.variant, sprite.archetype)
			var out_path := "res://iprop-export-seed%d-variant%d.png" % [sprite.seed, sprite.variant]
			image.save_png(out_path)
			print("exported ", out_path)
