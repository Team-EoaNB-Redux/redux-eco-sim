extends RefCounted
## Pop standard of living (heat map).


static func info() -> Dictionary:
	return {"id": "standard_of_living", "title": "Standard of living", "order": 60}


static func colors(ids: PackedInt32Array) -> PackedColorArray:
	return MapModes.heat_of(ids, "{row}.pops_sol")
