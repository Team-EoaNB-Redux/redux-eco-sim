extends EcoPanel
## Budget: tax and spending levels, with buttons that call the mod's own change_* effects (the same effects
## the in-game budget GUI uses; input = percent_change_temp).

const TAXES := [
	["Upper strata income tax", "income_tax_upper_strata_percentage", "change_upper_strata_income_tax"],
	["Middle strata income tax", "income_tax_middle_strata_percentage", "change_middle_strata_income_tax"],
	["Lower strata income tax", "income_tax_lower_strata_percentage", "change_lower_strata_income_tax"],
	["Sales tax", "sales_tax_percentage", "change_sales_tax"],
	["Corporate tax", "corporate_tax_percentage", "change_corporate_tax"],
	["Import tariff", "import_tariff_percentage", "change_import_tariff"],
	["Export tariff", "export_tariff_percentage", "change_export_tariff"],
]
const SPENDING := [
	["Military", "military_spending_percentage", "change_military_spending"],
	["Procurement", "procurement_spending_percentage", "change_procurement_spending"],
	["Intelligence", "intelligence_spending_percentage", "change_intelligence_spending"],
	["Construction", "construction_spending_percentage", "change_construction_spending"],
	["Research", "research_spending_percentage", "change_research_spending"],
	["Infrastructure subsidy", "infrastructure_subsidy_percentage", "change_infrastructure_subsidy"],
	["Industrial subsidy", "industrial_subsidy_percentage", "change_industrial_subsidy"],
]


static func info() -> Dictionary:
	return {"id": "budget", "title": "Budget", "order": 20}


func build(b: PanelBuilder) -> void:
	b.section("Budget")
	b.stats([
		["Net budget / month", "{country}.net_budget"],
		["Treasury", "{country}.treasury"],
		["National debt", "{country}.national_debt"],
		["Debt / GDP", "{country}.debt_to_gdp_ratio"],
	])
	_levels(b, "Taxes (%)", TAXES)
	_levels(b, "Spending (%)", SPENDING)
	b.chart("Net budget", ["{country}.net_budget"])


func _levels(b: PanelBuilder, title: String, rows: Array) -> void:
	b.section(title)
	for r in rows:
		b.stat(r[0], "{country}." + r[1], 1)
		var buttons := []
		for step in [-10, -1, 1, 10]:
			buttons.append([("%+d" % step), "set_temp_variable = { percent_change_temp = %d } %s = yes" % [step, r[2]]])
		b.effect_row("", buttons)
