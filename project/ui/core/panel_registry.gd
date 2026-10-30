class_name PanelRegistry
extends RefCounted
## Discovers panels: every res://ui/panels/<folder>/panel.gd that extends EcoPanel and defines
## `static func info() -> Dictionary`. Sorted by info().order. No list to maintain.

const PANELS_DIR := "res://ui/panels"

## Panel scripts that failed to load in the last discover() (the --smoke test fails on any).
static var failed: PackedStringArray = []


static func discover() -> Array[Dictionary]:
	var found: Array[Dictionary] = []
	failed.clear()
	var dir := DirAccess.open(PANELS_DIR)
	if dir == null:
		push_error("PanelRegistry: cannot open " + PANELS_DIR)
		return found
	for folder in dir.get_directories():
		var path := PANELS_DIR.path_join(folder).path_join("panel.gd")
		if not ResourceLoader.exists(path):
			continue
		var script: GDScript = load(path)
		if script == null or not script.can_instantiate():
			push_error("PanelRegistry: cannot load " + path)
			failed.append(path)
			continue
		var meta: Dictionary = {}
		for m in script.get_script_method_list():
			if m["name"] == "info":
				meta = (script.call("info") as Dictionary).duplicate()
				break
		meta["script"] = script
		meta["path"] = path
		if not meta.has("id"):
			meta["id"] = folder
		if not meta.has("title"):
			meta["title"] = folder.capitalize()
		if not meta.has("order"):
			meta["order"] = 100
		found.append(meta)
	found.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return a["order"] < b["order"])
	return found


static func instantiate(meta: Dictionary) -> EcoPanel:
	var panel: EcoPanel = (meta["script"] as GDScript).new()
	panel.name = str(meta["id"])
	panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	return panel
