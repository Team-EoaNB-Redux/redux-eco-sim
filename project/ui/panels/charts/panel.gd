extends EcoPanel
## Free-form watches: type any HOI4 reference ("ENG.price@token:steel", "952.base_wage") to chart it.
## Watched series are sampled after every simulated month (history starts when the watch is added).

var _input: LineEdit
var _list: ItemList
var _chart: LineChart


static func info() -> Dictionary:
	return {"id": "charts", "title": "Charts", "order": 70}


func build(b: PanelBuilder) -> void:
	b.section("Watches")
	b.note("Add a reference; {country} and {state} expand to the current selection. Select a watch and press Delete to remove it.")
	var row := HBoxContainer.new()
	_input = LineEdit.new()
	_input.placeholder_text = "{country}.gdp_amount"
	_input.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_input.text_submitted.connect(func(_t: String) -> void: _add())
	var add := Button.new()
	add.text = "Add watch"
	add.pressed.connect(_add)
	row.add_child(_input)
	row.add_child(add)
	add_child(row)
	_list = ItemList.new()
	_list.custom_minimum_size.y = 110
	_list.gui_input.connect(_on_list_input)
	add_child(_list)
	_chart = LineChart.new()
	_chart.custom_minimum_size = Vector2(400, 360)
	_chart.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	add_child(_chart)


func _add() -> void:
	var r := Sim.ref(_input.text.strip_edges())
	if r.is_empty():
		return
	Sim.eco.add_watch(r)
	_input.clear()
	refresh()


func _on_list_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and event.keycode == KEY_DELETE:
		for i in _list.get_selected_items():
			Sim.eco.remove_watch(_list.get_item_text(i))
		refresh()


func refresh() -> void:
	_list.clear()
	var series := {}
	for w in Sim.eco.get_watches():
		_list.add_item(w)
		series[w] = Sim.eco.get_series(w)
	_chart.set_series(series)
