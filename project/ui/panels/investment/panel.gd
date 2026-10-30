extends EcoPanel
## Investment fund and companies of the selected country.


static func info() -> Dictionary:
	return {"id": "investment", "title": "Investment", "order": 50}


func build(b: PanelBuilder) -> void:
	b.section("Investment fund")
	b.stats([
		["Fund", "{country}.investment_fund"],
		["Target spending", "{country}.investment_fund_target_spending"],
		["Unspent last tick", "{country}.investment_fund_unspent"],
		["Active opportunities", "{country}.active_investment_opportunities^num"],
		["Active national opportunity", "{country}.active_national_opportunity"],
	])
	b.note("Investment opportunity ids (records live in the country's investment_* variables; open the Inspector filtered on 'investment_opportunity').")
	b.table("Active investment opportunities", "{country}.active_investment_opportunities", [
		["Attractiveness", "{country}.investment_opportunity_attractiveness@{row}"],
		["Cost", "{country}.investment_opportunity_cost@{row}"],
		["Pooled", "{country}.investment_opportunity_investment@{row}"],
	])
	b.section("Companies")
	b.stat("Companies owned", "{country}.companies^num", 0)
	b.table("", "{country}.companies", [
		["Treasury", "global.company_treasury@{row}"],
		["Net cashflow", "global.company_net_cashflow@{row}"],
		["Dividends", "global.company_dividends@{row}"],
		["Total size", "global.company_total_size@{row}"],
	])
	b.chart("Fund", ["{country}.investment_fund"])
