extends EcoPanel
## State map of the loaded scenario (HOI4 provinces.bmp). Click a state to select it ({state} context).
## Wheel zooms around the cursor, WASD / arrow keys pan (Shift = faster), middle or right mouse drags.
## Map modes are auto-discovered from res://ui/map_modes/*.gd.

const DOWNSCALE := 2
const PAN_SPEED := 900.0 # screen pixels per second

var _modes: Array[Dictionary] = []
var _mode_pick: OptionButton
var _scroll: ScrollContainer
var _rect: TextureRect
var _mat: ShaderMaterial
var _palette_tex: ImageTexture
var _ids := PackedInt32Array()
var _index_of := {}
var _map_size := Vector2i.ZERO
var _zoom := 0.45
var _status: Label
var _hover_text := ""
var _dragging := false


static func info() -> Dictionary:
	return {"id": "map", "title": "Map", "order": 5}


func build(_b: PanelBuilder) -> void:
	var bar := HBoxContainer.new()
	_mode_pick = OptionButton.new()
	_modes = MapModes.discover()
	for m in _modes:
		_mode_pick.add_item(m["title"])
	_mode_pick.item_selected.connect(func(_i: int) -> void: refresh())
	bar.add_child(_mode_pick)
	_status = Label.new()
	bar.add_child(_status)
	add_child(bar)
	_scroll = ScrollContainer.new()
	_scroll.custom_minimum_size = Vector2(800, 720)
	_scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	add_child(_scroll)
	_rect = TextureRect.new()
	_rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_rect.stretch_mode = TextureRect.STRETCH_SCALE
	_rect.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	_rect.mouse_filter = Control.MOUSE_FILTER_STOP
	_rect.gui_input.connect(_on_map_input)
	_scroll.add_child(_rect)
	_mat = ShaderMaterial.new()
	_mat.shader = load("res://ui/map/state_map.gdshader")
	_rect.material = _mat


func on_loaded() -> void:
	var err := Sim.eco.build_state_map(DOWNSCALE)
	if not err.is_empty():
		_status.text = "map: " + err
		return
	_map_size = Sim.eco.get_state_map_size()
	_rect.texture = ImageTexture.create_from_image(Sim.eco.get_state_map_image())
	_ids = Sim.eco.get_state_map_ids()
	_index_of.clear()
	for i in _ids.size():
		_index_of[_ids[i]] = i
	_apply_zoom()
	_focus_state(Sim.state)
	refresh()


func refresh() -> void:
	if _ids.is_empty() or _modes.is_empty():
		return
	var mode: Dictionary = _modes[maxi(_mode_pick.selected, 0)]
	var cols: PackedColorArray = (mode["script"] as GDScript).call("colors", _ids)
	var img := Image.create(maxi(_ids.size(), 1), 1, false, Image.FORMAT_RGBA8)
	for i in cols.size():
		img.set_pixel(i, 0, cols[i])
	if _palette_tex == null or _palette_tex.get_width() != img.get_width():
		_palette_tex = ImageTexture.create_from_image(img)
	else:
		_palette_tex.update(img)
	_mat.set_shader_parameter("palette_tex", _palette_tex)
	_mat.set_shader_parameter("selected_id", _index_of.get(Sim.state, -1))
	_update_status()


func _update_status() -> void:
	if _modes.is_empty():
		return
	var mode: Dictionary = _modes[maxi(_mode_pick.selected, 0)]
	_status.text = "  %d states · %s · selected %s%s" % [_ids.size() - 1, mode["title"], _state_label(Sim.state),
		("   ·   " + _hover_text) if _hover_text != "" else ""]


func _state_label(sid: int) -> String:
	var info := Sim.eco.get_state_info(sid)
	if info.is_empty():
		return str(sid)
	var owner: String = info.get("owner", "")
	return "%d %s (%s %s)" % [sid, info.get("label", ""), owner, Sim.eco.get_country_name(owner)]


func _process(delta: float) -> void:
	if not is_visible_in_tree() or _ids.is_empty():
		return
	var focus := get_viewport().gui_get_focus_owner()
	if focus is LineEdit or focus is TextEdit:
		return
	var dir := Vector2.ZERO
	if Input.is_physical_key_pressed(KEY_A) or Input.is_physical_key_pressed(KEY_LEFT): dir.x -= 1
	if Input.is_physical_key_pressed(KEY_D) or Input.is_physical_key_pressed(KEY_RIGHT): dir.x += 1
	if Input.is_physical_key_pressed(KEY_W) or Input.is_physical_key_pressed(KEY_UP): dir.y -= 1
	if Input.is_physical_key_pressed(KEY_S) or Input.is_physical_key_pressed(KEY_DOWN): dir.y += 1
	if dir == Vector2.ZERO:
		return
	var step := dir.normalized() * PAN_SPEED * delta * (3.0 if Input.is_physical_key_pressed(KEY_SHIFT) else 1.0)
	_scroll.scroll_horizontal += int(step.x)
	_scroll.scroll_vertical += int(step.y)


func _apply_zoom() -> void:
	_rect.custom_minimum_size = Vector2(_map_size) * _zoom


func _focus_state(id: int) -> void:
	var c := Sim.eco.get_state_centroid(id)
	if c.x < 0.0:
		return
	await get_tree().process_frame
	_scroll.scroll_horizontal = int(c.x * _zoom - _scroll.size.x / 2.0)
	_scroll.scroll_vertical = int(c.y * _zoom - _scroll.size.y / 2.0)


func _pixel_at(local: Vector2) -> Vector2i:
	return Vector2i(int(local.x / _zoom), int(local.y / _zoom))


func _on_map_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		var p := _pixel_at(event.position)
		var sid := Sim.eco.get_state_at_pixel(p.x, p.y)
		_mat.set_shader_parameter("hovered_id", _index_of.get(sid, -1))
		if _dragging:
			_scroll.scroll_horizontal -= int(event.relative.x)
			_scroll.scroll_vertical -= int(event.relative.y)
		var text := "" if sid < 0 else _state_label(sid)
		if text != _hover_text:
			_hover_text = text
			_rect.tooltip_text = text
			_update_status()
	elif event is InputEventMouseButton and (event.button_index == MOUSE_BUTTON_MIDDLE or event.button_index == MOUSE_BUTTON_RIGHT):
		_dragging = event.pressed
		_rect.accept_event()
	elif event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_LEFT:
			var p := _pixel_at(event.position)
			var sid := Sim.eco.get_state_at_pixel(p.x, p.y)
			if sid >= 0:
				Sim.set_state(sid)
		elif event.button_index == MOUSE_BUTTON_WHEEL_UP or event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			# keep the map point under the cursor fixed while zooming
			var screen: Vector2 = event.position - Vector2(_scroll.scroll_horizontal, _scroll.scroll_vertical)
			var map_pt: Vector2 = event.position / _zoom
			_zoom = clampf(_zoom * (1.15 if event.button_index == MOUSE_BUTTON_WHEEL_UP else 1.0 / 1.15), 0.15, 4.0)
			_apply_zoom()
			_rect.accept_event()
			await get_tree().process_frame # let the ScrollContainer pick up the new size
			_scroll.scroll_horizontal = int(map_pt.x * _zoom - screen.x)
			_scroll.scroll_vertical = int(map_pt.y * _zoom - screen.y)
