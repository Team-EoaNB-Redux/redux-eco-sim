extends EcoPanel
## Script `log` output (game.log style) and the scripted-effect profile (inclusive time).

var _log: TextEdit
var _profile: Tree


static func info() -> Dictionary:
	return {"id": "log_profile", "title": "Log & Profile", "order": 99}


func build(b: PanelBuilder) -> void:
	b.section("Script log (last 300 lines)")
	_log = TextEdit.new()
	_log.editable = false
	_log.custom_minimum_size.y = 240
	add_child(_log)
	b.section("Profile — scripted effects by inclusive time")
	_profile = Tree.new()
	_profile.columns = 3
	_profile.hide_root = true
	_profile.column_titles_visible = true
	_profile.set_column_title(0, "scripted effect")
	_profile.set_column_title(1, "calls")
	_profile.set_column_title(2, "seconds")
	_profile.custom_minimum_size.y = 320
	add_child(_profile)


func refresh() -> void:
	var n := Sim.eco.get_log_size()
	_log.text = "\n".join(Sim.eco.get_log(maxi(0, n - 300)))
	_log.scroll_vertical = _log.get_line_count()
	_profile.clear()
	var root := _profile.create_item()
	for p in Sim.eco.get_profile(40):
		var item := _profile.create_item(root)
		item.set_text(0, p["name"])
		item.set_text(1, str(p["calls"]))
		item.set_text(2, "%.3f" % p["seconds"])
