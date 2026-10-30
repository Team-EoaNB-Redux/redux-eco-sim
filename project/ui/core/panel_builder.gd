class_name PanelBuilder
extends RefCounted
## Declarative helpers for panels. Every widget is bound to HOI4 variable references (templates with
## {country} / {state} / {row} placeholders) and refreshes itself after each tick or selection change.
##
##   section(title)                                 bold header
##   note(text)                                     grey help text
##   stat(label, ref, decimals := 2)                label: value
##   stats(pairs: [[label, ref], ...], decimals)    two-column grid of stats
##   table(title, rows_ref, columns, row_label)     rows = elements of an array ref (tokens / tags / ids);
##                                                  columns = [[header, ref_template_with_{row}], ...]
##   list_table(title, rows: Array[String], columns, row_label)   same, with a fixed row list
##   chart(title, refs: Array[String])              line chart of watched series (sampled monthly)
##   button(label, effect_text, scope := "{country}")   runs effect text in a scope (like the in-game console)
##   effect_row(label, [[button_text, effect_text], ...], scope)

var panel: Control
var _refreshers: Array[Callable] = []


func _init(p: Control) -> void:
	panel = p


func refresh() -> void:
	for r in _refreshers:
		r.call()


static func fmt(v: float, decimals: int = 2) -> String:
	if is_nan(v):
		return "—"
	var a := absf(v)
	if a >= 1e9:
		return "%.2fB" % (v / 1e9)
	if a >= 1e6:
		return "%.2fM" % (v / 1e6)
	if a >= 1e4:
		return "%.1fK" % (v / 1e3)
	return ("%." + str(decimals) + "f") % v


func section(title: String) -> Label:
	var l := Label.new()
	l.text = title
	l.add_theme_font_size_override("font_size", 18)
	l.add_theme_color_override("font_color", Color(0.95, 0.85, 0.55))
	panel.add_child(l)
	return l


func note(text: String) -> Label:
	var l := Label.new()
	l.text = text
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.add_theme_color_override("font_color", Color(0.65, 0.65, 0.7))
	panel.add_child(l)
	return l


func stat(label: String, ref: String, decimals: int = 2) -> void:
	stats([[label, ref]], decimals)


func stats(pairs: Array, decimals: int = 2) -> void:
	var grid := GridContainer.new()
	grid.columns = 2
	grid.add_theme_constant_override("h_separation", 24)
	panel.add_child(grid)
	for p in pairs:
		var name_label := Label.new()
		name_label.text = p[0]
		name_label.tooltip_text = p[1]
		name_label.mouse_filter = Control.MOUSE_FILTER_PASS
		var value_label := Label.new()
		value_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		value_label.custom_minimum_size.x = 120
		grid.add_child(name_label)
		grid.add_child(value_label)
		var ref: String = p[1]
		_refreshers.append(func() -> void:
			value_label.text = fmt(Sim.value(ref), decimals)
			value_label.tooltip_text = Sim.ref(ref) + " = " + Sim.text(ref))


func table(title: String, rows_ref: String, columns: Array, row_label: String = "{row}") -> Tree:
	return _table(title, func() -> PackedStringArray: return Sim.eco.get_array(Sim.ref(rows_ref)), columns, row_label)


func list_table(title: String, rows: Array, columns: Array, row_label: String = "{row}") -> Tree:
	var fixed := PackedStringArray(rows)
	return _table(title, func() -> PackedStringArray: return fixed, columns, row_label)


func _table(title: String, rows_fn: Callable, columns: Array, row_label: String) -> Tree:
	if title != "":
		section(title)
	var tree := Tree.new()
	tree.columns = columns.size() + 1
	tree.column_titles_visible = true
	tree.hide_root = true
	tree.custom_minimum_size.y = 220
	tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	tree.set_column_title(0, "")
	tree.set_column_expand(0, true)
	for i in columns.size():
		tree.set_column_title(i + 1, columns[i][0])
		tree.set_column_title_alignment(i + 1, HORIZONTAL_ALIGNMENT_RIGHT)
	panel.add_child(tree)
	_refreshers.append(func() -> void:
		tree.clear()
		var root := tree.create_item()
		for row in rows_fn.call():
			var item := tree.create_item(root)
			item.set_text(0, row_label.replace("{row}", row))
			for i in columns.size():
				var r := Sim.ref(columns[i][1], row)
				item.set_text(i + 1, fmt(Sim.eco.get_value(r)))
				item.set_text_alignment(i + 1, HORIZONTAL_ALIGNMENT_RIGHT)
				item.set_tooltip_text(i + 1, r))
	return tree


func chart(title: String, refs: Array) -> LineChart:
	if title != "":
		section(title)
	var c := LineChart.new()
	c.custom_minimum_size = Vector2(400, 220)
	c.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	panel.add_child(c)
	_refreshers.append(func() -> void:
		var series := {}
		for template in refs:
			var r := Sim.ref(template)
			Sim.eco.add_watch(r)
			series[r] = Sim.eco.get_series(r)
		c.set_series(series))
	return c


func button(label: String, effect_text: String, scope: String = "{country}") -> Button:
	var b := Button.new()
	b.text = label
	b.tooltip_text = effect_text
	b.pressed.connect(func() -> void:
		Sim.eco.run_effect(Sim.ref(scope), Sim.ref(effect_text))
		Sim.ticked.emit())
	panel.add_child(b)
	return b


func effect_row(label: String, buttons: Array, scope: String = "{country}") -> void:
	var row := HBoxContainer.new()
	var l := Label.new()
	l.text = label
	l.custom_minimum_size.x = 220
	row.add_child(l)
	for spec in buttons:
		var b := Button.new()
		b.text = spec[0]
		b.tooltip_text = spec[1]
		var effect_text: String = spec[1]
		b.pressed.connect(func() -> void:
			Sim.eco.run_effect(Sim.ref(scope), Sim.ref(effect_text))
			Sim.ticked.emit())
		row.add_child(b)
	panel.add_child(row)
