extends RefCounted
## Owner of each state.


static func info() -> Dictionary:
	return {"id": "political", "title": "Political", "order": 10}


static func colors(ids: PackedInt32Array) -> PackedColorArray:
	var out := PackedColorArray()
	out.resize(ids.size())
	for i in ids.size():
		out[i] = MapModes.name_color(Sim.eco.get_state_info(ids[i]).get("owner", "")) if ids[i] >= 0 else Color.BLACK
	return out
