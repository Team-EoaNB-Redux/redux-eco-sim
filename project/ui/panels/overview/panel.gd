extends EcoPanel
## Country overview: headline aggregates + GDP / treasury charts for the selected country.


static func info() -> Dictionary:
	return {"id": "overview", "title": "Overview", "order": 10}


func build(b: PanelBuilder) -> void:
	b.section("Country")
	b.stats([
		["GDP", "{country}.gdp_amount"],
		["GDP growth %", "{country}.gdp_growth"],
		["Treasury", "{country}.treasury"],
		["National debt", "{country}.national_debt"],
		["Net budget / month", "{country}.net_budget"],
		["Investment fund", "{country}.investment_fund"],
	])
	b.section("Climate (modifier@)")
	b.stats([
		["Incentives", "{country}.modifier@incentives_modifier"],
		["Consumer confidence", "{country}.modifier@consumer_confidence_modifier"],
		["Investment confidence", "{country}.modifier@investment_confidence_modifier"],
		["Banking confidence", "{country}.modifier@banking_confidence_modifier"],
	], 1)
	b.chart("GDP", ["{country}.gdp_amount"])
	b.chart("Treasury & net budget", ["{country}.treasury", "{country}.net_budget"])
