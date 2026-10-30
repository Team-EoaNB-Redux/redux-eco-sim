extends RefCounted
## Market membership: colour by the owner's market leader (from each leader's market_countries array).


static func info() -> Dictionary:
	return {"id": "market", "title": "Market", "order": 20}


static func colors(ids: PackedInt32Array) -> PackedColorArray:
	var leader_of := {}
	for c in Sim.eco.list_countries():
		for member in Sim.eco.get_array("%s.market_countries" % c):
			leader_of[member] = c
	var out := PackedColorArray()
	out.resize(ids.size())
	for i in ids.size():
		if ids[i] < 0:
			out[i] = Color.BLACK
			continue
		var owner: String = Sim.eco.get_state_info(ids[i]).get("owner", "")
		out[i] = MapModes.name_color(leader_of.get(owner, owner))
	return out
