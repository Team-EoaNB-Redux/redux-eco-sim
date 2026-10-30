extends RefCounted
## Floating base wage (heat map).


static func info() -> Dictionary:
	return {"id": "base_wage", "title": "Base wage", "order": 40}


static func colors(ids: PackedInt32Array) -> PackedColorArray:
	return MapModes.heat_of(ids, "{row}.base_wage")
