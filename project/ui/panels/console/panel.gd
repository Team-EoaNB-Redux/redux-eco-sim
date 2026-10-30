extends EcoPanel
## Effect console — the in-game `effect` command: runs HOI4 effect script in a scope and shows `log` output.
## Example: log = "[?treasury|0]"   or   finance_tests = yes   (in a country scope)

var _scope: LineEdit
var _code: TextEdit
var _out: TextEdit


static func info() -> Dictionary:
	return {"id": "console", "title": "Console", "order": 90}


func build(b: PanelBuilder) -> void:
	b.section("Effect console")
	b.note("Runs effect script in the scope below (tag, state id or global). Use log = \"[?var|2]\" to print values.")
	_scope = LineEdit.new()
	_scope.text = "{country}"
	add_child(_scope)
	_code = TextEdit.new()
	_code.custom_minimum_size.y = 160
	_code.text = "log = \"GDP [?gdp_amount|0], treasury [?treasury|0]\""
	add_child(_code)
	var run := Button.new()
	run.text = "Run effect (Ctrl+Enter)"
	run.pressed.connect(_run)
	add_child(run)
	_code.gui_input.connect(func(e: InputEvent) -> void:
		if e is InputEventKey and e.pressed and e.keycode == KEY_ENTER and e.ctrl_pressed:
			_run()
			_code.accept_event())
	_out = TextEdit.new()
	_out.editable = false
	_out.custom_minimum_size.y = 260
	_out.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(_out)


func _run() -> void:
	var lines := Sim.eco.run_effect(Sim.ref(_scope.text.strip_edges()), _code.text)
	_out.text += "> %s\n%s\n" % [Sim.ref(_scope.text.strip_edges()), "\n".join(lines)]
	_out.scroll_vertical = _out.get_line_count()
	Sim.ticked.emit()
