extends Control
## App shell, built in code (easier for AI edits than .tscn files):
##   top bar   — scenario, date, time controls, speed
##   left      — country picker + state list (sets the {country} / {state} context)
##   centre    — one tab per discovered panel (res://ui/panels/*/panel.gd)

var _date: Label
var _status: Label
var _scenario: LineEdit
var _countries: OptionButton
var _states: ItemList
var _tabs: TabContainer
var _panels: Array[EcoPanel] = []
var _time_buttons: Array[Button] = []


func _ready() -> void:
	_build_ui()
	Sim.loaded.connect(_on_loaded)
	Sim.ticked.connect(_refresh_all)
	Sim.context_changed.connect(_refresh_all)
	Sim.busy_changed.connect(_on_busy)
	await _load()
	if OS.get_cmdline_user_args().has("--smoke"):
		_smoke_test()


## Headless check of the real app path:
##   Godot_v4.5.1-stable_win64_console.exe --headless --path project -- --smoke
func _smoke_test() -> void:
	var ok := Sim.last_error.is_empty() and Sim.eco.is_loaded() and PanelRegistry.failed.is_empty()
	print("SMOKE load: ", "ok" if ok else Sim.last_error, " | panels: ", _panels.size(), " | countries: ", Sim.eco.list_countries().size(),
		"" if PanelRegistry.failed.is_empty() else " | FAILED panels: " + ", ".join(PanelRegistry.failed))
	if ok:
		Sim.run_months(1)
		while Sim.eco.is_busy():
			await get_tree().process_frame
		await get_tree().process_frame
		for i in _panels.size():
			_tabs.current_tab = i
			_panels[i].update_all()
		print("SMOKE date: ", Sim.eco.get_date(), " | ENG GDP: ", Sim.eco.get_value("ENG.gdp_amount"),
			" | watches: ", Sim.eco.get_watches().size())
		ok = Sim.eco.get_months_elapsed() == 1
		var args := OS.get_cmdline_user_args()
		var at := args.find("--smoke-map")
		if at >= 0 and at + 1 < args.size():
			_save_map_preview(args[at + 1])
	print("SMOKE ", "OK" if ok else "FAILED")
	get_tree().quit(0 if ok else 1)


func _build_ui() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var root := VBoxContainer.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(root)

	var bar := HBoxContainer.new()
	bar.add_theme_constant_override("separation", 8)
	root.add_child(bar)
	_scenario = LineEdit.new()
	_scenario.text = Sim.DEFAULT_SCENARIO
	_scenario.custom_minimum_size.x = 260
	_scenario.tooltip_text = "scenario file, relative to redux-eco-sim/"
	bar.add_child(_scenario)
	var load_btn := Button.new()
	load_btn.text = "Load"
	load_btn.pressed.connect(_load)
	bar.add_child(load_btn)
	_date = Label.new()
	_date.custom_minimum_size.x = 120
	_date.add_theme_font_size_override("font_size", 18)
	bar.add_child(_date)
	for spec in [["+1 week", 7, 0], ["+1 month", 0, 1], ["+6 months", 0, 6], ["+1 year", 0, 12], ["+10 years", 0, 120]]:
		var b := Button.new()
		b.text = spec[0]
		var days: int = spec[1]
		var months: int = spec[2]
		b.pressed.connect(func() -> void:
			if days > 0:
				Sim.run_days(days)
			else:
				Sim.run_months(months))
		bar.add_child(b)
		_time_buttons.append(b)
	var stop := Button.new()
	stop.text = "Stop"
	stop.pressed.connect(func() -> void: Sim.eco.cancel())
	bar.add_child(stop)
	_status = Label.new()
	_status.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_status.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	bar.add_child(_status)

	var split := HSplitContainer.new()
	split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	root.add_child(split)
	var side := VBoxContainer.new()
	side.custom_minimum_size.x = 220
	split.add_child(side)
	var cl := Label.new()
	cl.text = "Country"
	side.add_child(cl)
	_countries = OptionButton.new()
	_countries.item_selected.connect(func(i: int) -> void: Sim.set_country(_countries.get_item_metadata(i)); _fill_states())
	side.add_child(_countries)
	var sl := Label.new()
	sl.text = "States"
	side.add_child(sl)
	_states = ItemList.new()
	_states.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_states.item_selected.connect(func(i: int) -> void: Sim.set_state(_states.get_item_metadata(i)))
	side.add_child(_states)

	_tabs = TabContainer.new()
	_tabs.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_tabs.tab_changed.connect(func(_i: int) -> void: _refresh_current())
	split.add_child(_tabs)
	for meta in PanelRegistry.discover():
		var scroll := ScrollContainer.new()
		scroll.name = str(meta["title"])
		scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
		var panel := PanelRegistry.instantiate(meta)
		scroll.add_child(panel)
		_tabs.add_child(scroll)
		panel.setup()
		_panels.append(panel)


## CPU-composited political map (for checking the map pipeline without a display).
func _save_map_preview(path: String) -> void:
	Sim.eco.build_state_map(8)
	var ids := Sim.eco.get_state_map_ids()
	var palette: PackedColorArray = load("res://ui/map_modes/political.gd").call("colors", ids)
	var src := Sim.eco.get_state_map_image()
	var out := Image.create(src.get_width(), src.get_height(), false, Image.FORMAT_RGB8)
	for y in src.get_height():
		for x in src.get_width():
			var c := src.get_pixel(x, y)
			var idx := int(round(c.r * 255.0)) + (int(round(c.g * 255.0)) << 8)
			out.set_pixel(x, y, palette[idx] if idx > 0 else Color(0.07, 0.09, 0.13))
	out.save_png(path)
	print("SMOKE map preview: ", path, " (", src.get_width(), "x", src.get_height(), ", ", ids.size() - 1, " states)")
	Sim.eco.build_state_map(2)


func _load() -> void:
	_status.text = "loading…"
	await get_tree().process_frame
	var err := Sim.load_scenario(_scenario.text.strip_edges())
	if not err.is_empty():
		_status.text = "load failed: " + err
		push_error(err)


func _on_loaded() -> void:
	_countries.clear()
	var list := Sim.eco.list_countries()
	for i in list.size():
		var cname := Sim.eco.get_country_name(list[i])
		_countries.add_item(list[i] if cname == list[i] else "%s  %s" % [list[i], cname])
		_countries.set_item_metadata(i, list[i])
		if list[i] == Sim.country:
			_countries.select(i)
	_fill_states()
	for p in _panels:
		p.on_loaded()
	_refresh_all()


func _fill_states() -> void:
	_states.clear()
	for id in Sim.eco.list_states(Sim.country):
		var info := Sim.eco.get_state_info(id)
		var idx := _states.add_item("%d %s" % [id, info.get("label", "")])
		_states.set_item_metadata(idx, id)
		if id == Sim.state:
			_states.select(idx)


func _on_busy(busy: bool) -> void:
	for b in _time_buttons:
		b.disabled = busy
	if busy:
		_status.text = "simulating…"


func _refresh_current() -> void:
	var i := _tabs.current_tab
	if i >= 0 and i < _panels.size():
		_panels[i].update_all()


func _refresh_all() -> void:
	if Sim.eco.is_busy():
		return
	_date.text = Sim.eco.get_date()
	var spm := Sim.eco.get_seconds_per_month()
	_status.text = "%s · %d months · %s" % [Sim.eco.get_scenario_name(), Sim.eco.get_months_elapsed(),
		("%.2f s/month" % spm) if spm > 0.0 else "ready"]
	_refresh_current()
