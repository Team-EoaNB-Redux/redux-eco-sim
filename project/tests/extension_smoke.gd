extends SceneTree

# Headless check that the GDExtension loads and its bound API answers.
# Run: Godot_v4.5.1-stable_win64_console.exe --headless --path project -s res://tests/extension_smoke.gd

func _initialize() -> void:
	var ok := true
	if not ClassDB.class_exists("EcoSim"):
		push_error("EcoSim class not registered: the GDExtension did not load")
		quit(1)
		return
	var sim = ClassDB.instantiate("EcoSim")
	print("EcoSim version: ", sim.get_version())
	var formatted: String = sim.format_fixed("1.234567")
	print("format_fixed(1.234567) = ", formatted)
	ok = ok and formatted == "1.23456"
	print("SMOKE ", "OK" if ok else "FAILED")
	quit(0 if ok else 1)
