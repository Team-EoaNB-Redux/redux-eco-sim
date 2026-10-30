extends EcoPanel
## Selected state: headline numbers, industries, pops.


static func info() -> Dictionary:
	return {"id": "state_economy", "title": "State", "order": 40}


var _header: Label


func build(b: PanelBuilder) -> void:
	_header = b.section("State")
	b.stats([
		["GDP", "{state}.gdp_amount"],
		["Base wage", "{state}.base_wage"],
		["Labor utilization", "{state}.labor_utilization"],
		["Pops (k)", "{state}.pops"],
		["Standard of living", "{state}.pops_sol"],
		["Arable land", "{state}.arable_land"],
	])
	b.table("Industries", "{state}.industries", [
		["Size", "{state}.size@token:{row}"],
		["Employed", "{state}.employed@token:{row}"],
		["Empl. target", "{state}.employment_target@token:{row}"],
		["Productivity", "{state}.productivity@token:{row}"],
		["Profit", "{state}.profit@token:{row}"],
		["Profitability", "{state}.profitability@token:{row}"],
		["Dividends", "{state}.dividends@token:{row}"],
		["Cash", "{state}.money_accumulation@token:{row}"],
	])
	b.table("Pops", "global.pops_array", [
		["Pops", "{state}.pops@token:{row}"],
		["Workforce", "{state}.pops_workforce@token:{row}"],
		["Employed", "{state}.pops_employed@token:{row}"],
		["Unemployed", "{state}.pops_unemployed@token:{row}"],
		["Earnings", "{state}.pops_earnings@token:{row}"],
		["Spending", "{state}.pops_spending@token:{row}"],
		["Savings", "{state}.pops_savings@token:{row}"],
		["SoL", "{state}.pops_sol@token:{row}"],
	])
	b.chart("State GDP & wage", ["{state}.gdp_amount", "{state}.base_wage"])


func refresh() -> void:
	var info := Sim.eco.get_state_info(Sim.state)
	if info.is_empty():
		_header.text = "State %d (not loaded)" % Sim.state
	else:
		_header.text = "State %d — %s  (owner %s, %s)" % [Sim.state, info["label"], info["owner"], info["category"]]
