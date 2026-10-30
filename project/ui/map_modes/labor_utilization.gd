extends RefCounted
## Jobs / workforce (heat map).


static func info() -> Dictionary:
	return {"id": "labor_utilization", "title": "Labor utilization", "order": 50}


static func colors(ids: PackedInt32Array) -> PackedColorArray:
	return MapModes.heat_of(ids, "{row}.labor_utilization")
