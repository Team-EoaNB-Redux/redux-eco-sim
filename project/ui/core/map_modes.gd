class_name MapModes
extends RefCounted
## Map-mode discovery + helpers. A map mode is ONE file: res://ui/map_modes/<id>.gd with
##   static func info() -> Dictionary                         # {"id", "title", "order"}
##   static func colors(ids: PackedInt32Array) -> PackedColorArray
## `ids[i]` is the state id drawn with palette index i (ids[0] = -1 = sea / not loaded).
## Heat-map modes are usually one line: return MapModes.heat_of(ids, "{row}.gdp_amount")

const MODES_DIR := "res://ui/map_modes"


static func discover() -> Array[Dictionary]:
	var found: Array[Dictionary] = []
	var dir := DirAccess.open(MODES_DIR)
	if dir == null:
		return found
	for file in dir.get_files():
		if not file.ends_with(".gd"):
			continue
		var script: GDScript = load(MODES_DIR.path_join(file))
		if script == null:
			continue
		var meta: Dictionary = {}
		for m in script.get_script_method_list():
			if m["name"] == "info":
				meta = (script.call("info") as Dictionary).duplicate()
		meta["script"] = script
		if not meta.has("title"):
			meta["title"] = file.get_basename().capitalize()
		if not meta.has("order"):
			meta["order"] = 100
		found.append(meta)
	found.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return a["order"] < b["order"])
	return found


## Stable pseudo-random colour for a tag / name.
static func name_color(name: String) -> Color:
	if name.is_empty():
		return Color(0.25, 0.25, 0.28)
	var h := float(hash(name) % 1000) / 1000.0
	return Color.from_hsv(h, 0.55, 0.85)


## Heat map of a per-state value reference ({row} = state id). Colours span the 2nd..98th percentile.
static func heat_of(ids: PackedInt32Array, template: String) -> PackedColorArray:
	var values := PackedFloat64Array()
	values.resize(ids.size())
	var sorted := []
	for i in ids.size():
		if ids[i] < 0:
			values[i] = NAN
			continue
		var v: float = Sim.eco.get_value(template.replace("{row}", str(ids[i])))
		values[i] = v
		if not is_nan(v):
			sorted.append(v)
	sorted.sort()
	var lo := 0.0
	var hi := 1.0
	if sorted.size() > 0:
		lo = sorted[int(sorted.size() * 0.02)]
		hi = sorted[mini(sorted.size() - 1, int(sorted.size() * 0.98))]
	if is_equal_approx(lo, hi):
		hi = lo + 1.0
	var out := PackedColorArray()
	out.resize(ids.size())
	for i in ids.size():
		if is_nan(values[i]):
			out[i] = Color(0.2, 0.2, 0.22)
			continue
		var t := clampf((values[i] - lo) / (hi - lo), 0.0, 1.0)
		# blue (low) -> green -> yellow (high)
		if t < 0.5:
			out[i] = Color(0.15, 0.25, 0.6).lerp(Color(0.2, 0.75, 0.45), t * 2.0)
		else:
			out[i] = Color(0.2, 0.75, 0.45).lerp(Color(0.97, 0.8, 0.2), (t - 0.5) * 2.0)
	return out
