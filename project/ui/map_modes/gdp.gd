extends RefCounted
## State GDP (heat map).


static func info() -> Dictionary:
	return {"id": "gdp", "title": "GDP", "order": 30}


static func colors(ids: PackedInt32Array) -> PackedColorArray:
	return MapModes.heat_of(ids, "{row}.gdp_amount")
