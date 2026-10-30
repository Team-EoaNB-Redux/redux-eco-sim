class_name EcoPanel
extends VBoxContainer
## Base class for every UI panel. A panel is ONE file: res://ui/panels/<id>/panel.gd (auto-discovered by
## PanelRegistry — no central list to edit). Removing a feature = deleting its folder.
##
## Implement in a subclass:
##   static func info() -> Dictionary:
##       return {"id": "budget", "title": "Budget", "order": 20}
##   func build(b: PanelBuilder) -> void:
##       b.section("Treasury")
##       b.stat("Treasury", "{country}.treasury")
##       b.table("Pop types", "global.pops_array", [["Pops", "{state}.pops@token:{row}"]])
##
## Optional overrides:
##   func refresh() -> void        # custom drawing; called after every tick / context change
##   func on_loaded() -> void      # a scenario was (re)loaded

var builder: PanelBuilder


func setup() -> void:
	builder = PanelBuilder.new(self)
	build(builder)
	builder.refresh()
	refresh()


## Declare content. Subclasses override.
func build(_b: PanelBuilder) -> void:
	pass


## Custom refresh logic. Subclasses may override; the builder's widgets refresh automatically.
func refresh() -> void:
	pass


func on_loaded() -> void:
	pass


func update_all() -> void:
	if builder:
		builder.refresh()
	refresh()
