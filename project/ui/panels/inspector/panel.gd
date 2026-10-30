extends EcoPanel
## Variable inspector: every variable and array of a scope (country tag, state id or "global"), filterable.
## Double-click a variable to watch it (Charts tab). Covers any new eco feature with zero UI work.

var _scope: LineEdit
var _filter: LineEdit
var _tree: Tree
var _modifier: LineEdit
var _breakdown: Label


static func info() -> Dictionary:
	return {"id": "inspector", "title": "Inspector", "order": 80}


func build(b: PanelBuilder) -> void:
	b.section("Variables")
	var row := HBoxContainer.new()
	_scope = LineEdit.new()
	_scope.placeholder_text = "scope: {country} / {state} / global"
	_scope.text = "{country}"
	_scope.custom_minimum_size.x = 220
	_filter = LineEdit.new()
	_filter.placeholder_text = "filter (substring)"
	_filter.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	var go := Button.new()
	go.text = "Refresh"
	go.pressed.connect(refresh)
	_scope.text_submitted.connect(func(_t: String) -> void: refresh())
	_filter.text_submitted.connect(func(_t: String) -> void: refresh())
	row.add_child(_scope)
	row.add_child(_filter)
	row.add_child(go)
	add_child(row)
	_tree = Tree.new()
	_tree.columns = 2
	_tree.hide_root = true
	_tree.column_titles_visible = true
	_tree.set_column_title(0, "variable")
	_tree.set_column_title(1, "value")
	_tree.custom_minimum_size.y = 420
	_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_tree.item_activated.connect(_on_activated)
	add_child(_tree)

	b.section("Modifier breakdown")
	var mrow := HBoxContainer.new()
	_modifier = LineEdit.new()
	_modifier.placeholder_text = "modifier name, e.g. incentives_modifier"
	_modifier.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_modifier.text_submitted.connect(func(_t: String) -> void: refresh())
	mrow.add_child(_modifier)
	add_child(mrow)
	_breakdown = Label.new()
	add_child(_breakdown)


func refresh() -> void:
	var scope := Sim.ref(_scope.text.strip_edges())
	var data := Sim.eco.get_scope_vars(scope, _filter.text.strip_edges())
	_tree.clear()
	var root := _tree.create_item()
	if data.is_empty():
		var item := _tree.create_item(root)
		item.set_text(0, "(no such scope: %s)" % scope)
		return
	var vars: Dictionary = data["vars"]
	var names := vars.keys()
	names.sort()
	var vroot := _tree.create_item(root)
	vroot.set_text(0, "variables (%d)" % names.size())
	for n in names:
		var item := _tree.create_item(vroot)
		item.set_text(0, n)
		item.set_text(1, PanelBuilder.fmt(vars[n], 3))
		item.set_metadata(0, scope + "." + n)
	var arrays: Dictionary = data["arrays"]
	var anames := arrays.keys()
	anames.sort()
	var aroot := _tree.create_item(root)
	aroot.set_text(0, "arrays (%d)" % anames.size())
	for n in anames:
		var item := _tree.create_item(aroot)
		item.set_text(0, n)
		item.set_text(1, "%d elements" % arrays[n])
		item.set_tooltip_text(1, ", ".join(Sim.eco.get_array(scope + "." + n).slice(0, 40)))
	if not _modifier.text.strip_edges().is_empty():
		var lines := []
		var total := 0.0
		for src in Sim.eco.get_modifier_breakdown(scope, _modifier.text.strip_edges()):
			lines.append("  %s: %s" % [src["source"], PanelBuilder.fmt(src["value"], 3)])
			total += src["value"]
		_breakdown.text = "modifier@%s = %s\n%s" % [_modifier.text, PanelBuilder.fmt(total, 3), "\n".join(lines)]


func _on_activated() -> void:
	var item := _tree.get_selected()
	if item and item.get_metadata(0):
		Sim.eco.add_watch(str(item.get_metadata(0)))
