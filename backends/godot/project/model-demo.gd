# The model demo (preview): six chairs from one .iprop3d, each from its own
# seed, turning under a lamp that follows the mouse. Space rolls new seeds,
# the left and right arrows step them, F and C step the wood finish and the
# cushion fabric (0 leaves the choice to the seed).
extends Node3D

const CHAIRS := 6
const SPACING := 0.84

var model: IpropModel
var chairs: Array[IpropModelInstance3D] = []
var lamp: SpotLight3D
var camera: Camera3D
var finish := 0
var fabric := 0
# The lamp follows the mouse once it has moved over the window.
var pointing := false
const REST := Vector3(-1.4, 3.0, 1.7)


func _ready() -> void:
	model = IpropModel.new()
	if model.load_file("res://assets/chair.iprop3d") != OK:
		push_error("cannot load the chair")
		return

	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.106, 0.129, 0.157)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.86, 0.89, 0.96)
	environment.ambient_light_energy = 0.42
	environment.tonemap_mode = Environment.TONE_MAPPER_ACES
	var world := WorldEnvironment.new()
	world.environment = environment
	add_child(world)

	var floor_mesh := PlaneMesh.new()
	floor_mesh.size = Vector2(40, 40)
	var floor_material := StandardMaterial3D.new()
	floor_material.albedo_color = Color(0.165, 0.196, 0.235)
	floor_material.roughness = 0.92
	floor_mesh.material = floor_material
	var floor := MeshInstance3D.new()
	floor.mesh = floor_mesh
	add_child(floor)

	# Enough light everywhere to read a chair's colour; the lamp adds the drama.
	var fill := DirectionalLight3D.new()
	fill.light_color = Color(1.0, 0.96, 0.9)
	fill.light_energy = 0.22
	add_child(fill)
	fill.look_at_from_position(Vector3(-2, 3, 4), Vector3.ZERO)

	camera = Camera3D.new()
	camera.fov = 26.0
	add_child(camera)
	camera.look_at_from_position(Vector3(0, 2.0, 6.6), Vector3(0, 0.5, 0))

	lamp = SpotLight3D.new()
	lamp.light_color = Color(1.0, 0.88, 0.69)
	lamp.light_energy = 1.9
	lamp.spot_range = 14.0
	lamp.spot_angle = 47.0
	lamp.spot_angle_attenuation = 0.6
	lamp.shadow_enabled = true
	add_child(lamp)
	lamp.look_at_from_position(REST, Vector3(0, 0.35, 0))

	for i in CHAIRS:
		var chair := IpropModelInstance3D.new()
		chair.position = Vector3((i - (CHAIRS - 1) / 2.0) * SPACING, 0, 0)
		chair.rotation.y = 0.55 + fmod(i * 2.4, 1.3) - 0.6
		chair.seed = randi_range(1, 99999)
		chair.model = model
		add_child(chair)
		chairs.append(chair)


func _process(delta: float) -> void:
	for chair in chairs:
		chair.rotation.y += delta * 0.3
	# The lamp hangs above the floor under the mouse, a little toward the
	# viewer; without a mouse over the window it rests over the room.
	var goal := REST
	var aim := Vector3(0, 0.35, 0)
	if pointing:
		var mouse := get_viewport().get_mouse_position()
		var origin := camera.project_ray_origin(mouse)
		var direction := camera.project_ray_normal(mouse)
		if direction.y < -0.01:
			var hit := origin - direction * (origin.y / direction.y)
			hit.x = clampf(hit.x, -4.0, 4.0)
			hit.z = clampf(hit.z, -3.0, 2.5)
			goal = Vector3(hit.x, 2.9, hit.z + 1.3)
			aim = Vector3(hit.x, 0.35, hit.z)
	lamp.look_at_from_position(lamp.position.lerp(goal, 1.0 - exp(-delta * 9.0)), aim)


func _notification(what: int) -> void:
	if what == NOTIFICATION_WM_MOUSE_EXIT:
		pointing = false


func _input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		pointing = true


func _unhandled_input(event: InputEvent) -> void:
	if model == null or not (event is InputEventKey and event.pressed and not event.echo):
		return
	if event.keycode == KEY_SPACE:
		for chair in chairs:
			chair.seed = randi_range(1, 99999)
	elif event.keycode == KEY_LEFT or event.keycode == KEY_RIGHT:
		for chair in chairs:
			chair.seed = clampi(chair.seed + (1 if event.keycode == KEY_RIGHT else -1), 0, 4294967295)
	elif event.keycode == KEY_F:
		finish = (finish + 1) % 7
		model.set_input("finish", finish)
	elif event.keycode == KEY_C:
		fabric = (fabric + 1) % 7
		model.set_input("fabric", fabric)
