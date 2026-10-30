extends Node
## Autoload "Sim": the single owner of the EcoSim GDExtension object and of the UI's scope context
## (selected country / state). Panels never hold their own EcoSim; they call through this service.
##
## Refs are HOI4 variable references with a scope prefix ("ENG.treasury", "952.base_wage"). Templates may
## use {country} and {state}, which resolve to the current selection.

signal loaded            ## a scenario finished loading + startup
signal ticked            ## simulated time advanced (after an async run completes)
signal context_changed   ## the selected country or state changed
signal busy_changed(busy: bool)

const DEFAULT_SCENARIO := "scenarios/eng_1857.txt"

var eco: EcoSim
var country: String = "ENG"
var state: int = 952
var repo_dir: String = ""   ## redux-eco-sim/ (parent of the Godot project)
var mod_root: String = ""
var last_error: String = ""
var _was_busy := false


func _ready() -> void:
	eco = EcoSim.new()
	repo_dir = ProjectSettings.globalize_path("res://").get_base_dir().get_base_dir()
	mod_root = eco.find_mod_root(repo_dir)


func _process(_delta: float) -> void:
	if eco == null:
		return
	var busy := eco.is_busy()
	if busy != _was_busy:
		_was_busy = busy
		busy_changed.emit(busy)
	if eco.poll_finished():
		ticked.emit()


## Loads a scenario (path relative to redux-eco-sim/ or absolute) and runs on_startup. Returns "" or an error.
func load_scenario(scenario: String = DEFAULT_SCENARIO, semantics: String = "") -> String:
	if mod_root.is_empty():
		last_error = "mod root (toi/) not found above " + repo_dir
		return last_error
	var path := scenario if scenario.is_absolute_path() else repo_dir.path_join(scenario)
	last_error = eco.load_scenario(mod_root, path, semantics)
	if not last_error.is_empty():
		return last_error
	eco.startup()
	var countries := eco.list_countries()
	if not countries.has(country) and countries.size() > 0:
		country = countries[0]
	var states := eco.list_states(country)
	if not states.has(state) and states.size() > 0:
		state = states[0]
	loaded.emit()
	return ""


func run_months(months: int) -> void:
	if eco.is_loaded() and not eco.is_busy():
		eco.run_months_async(months)


func run_days(days: int) -> void:
	if eco.is_loaded() and not eco.is_busy():
		eco.run_days_async(days)


func set_country(tag: String) -> void:
	country = tag
	var states := eco.list_states(tag)
	if states.size() > 0 and not states.has(state):
		state = states[0]
	context_changed.emit()


func set_state(id: int) -> void:
	state = id
	var info := eco.get_state_info(id)
	if info.has("controller") and info["controller"] != "":
		country = info["controller"]
	context_changed.emit()


## Expands {country} / {state} / {row} placeholders.
func ref(template: String, row: String = "") -> String:
	return template.replace("{country}", country).replace("{state}", str(state)).replace("{row}", row)


func value(template: String, row: String = "") -> float:
	return eco.get_value(ref(template, row))


func text(template: String, row: String = "") -> String:
	return eco.get_value_text(ref(template, row))
