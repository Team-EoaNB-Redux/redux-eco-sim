extends EcoPanel
## Diagnostics: everything the game hides (unknown effects, failed math expressions, overflow, div by zero,
## unset scopes, ...), with file:line and hit counts.

var _severity: OptionButton
var _eco_only: CheckBox
var _tree: Tree


static func info() -> Dictionary:
	return {"id": "diagnostics", "title": "Diagnostics", "order": 95}


func build(b: PanelBuilder) -> void:
	b.section("Diagnostics")
	var row := HBoxContainer.new()
	_severity = OptionButton.new()
	for s in ["info+", "lint+", "warning+", "error"]:
		_severity.add_item(s)
	_severity.select(1)
	_severity.item_selected.connect(func(_i: int) -> void: refresh())
	_eco_only = CheckBox.new()
	_eco_only.text = "economy files only"
	_eco_only.button_pressed = true
	_eco_only.toggled.connect(func(_b: bool) -> void: refresh())
	row.add_child(_severity)
	row.add_child(_eco_only)
	add_child(row)
	_tree = Tree.new()
	_tree.columns = 4
	_tree.hide_root = true
	_tree.column_titles_visible = true
	for i in 4:
		_tree.set_column_title(i, ["kind", "where", "hits", "message"][i])
	_tree.set_column_expand(3, true)
	_tree.custom_minimum_size.y = 520
	_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(_tree)


func refresh() -> void:
	_tree.clear()
	var root := _tree.create_item()
	for d in Sim.eco.get_diagnostics(_severity.selected):
		if _eco_only.button_pressed and not str(d["file"]).contains("_economy_"):
			continue
		var item := _tree.create_item(root)
		item.set_text(0, "%s %s" % [d["severity"], d["kind"]])
		item.set_text(1, "%s:%d" % [d["file"], d["line"]])
		item.set_text(2, str(d["count"]))
		item.set_text(3, d["message"])
		item.set_tooltip_text(3, d["message"])
