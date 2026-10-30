extends EcoPanel
## Market of the selected country's market leader: per-resource price, supply, demand, balance, trade.


static func info() -> Dictionary:
	return {"id": "market", "title": "Market", "order": 30}


func build(b: PanelBuilder) -> void:
	b.note("Values are read from the market leader ({country}.market_leader). Trade < 0 = exports.")
	b.table("Resources", "global.resource_token_array", [
		["Price", "{country}.market_leader:price@token:{row}"],
		["vs base", "{country}.market_leader:price_compared_to_base@token:{row}"],
		["Supply", "{country}.market_leader:supply@token:{row}"],
		["Demand", "{country}.market_leader:demand@token:{row}"],
		["Balance", "{country}.market_leader:balance@token:{row}"],
		["Trade", "{country}.market_leader:trade@token:{row}"],
	])
	b.chart("Key prices", [
		"{country}.market_leader:price@token:food",
		"{country}.market_leader:price@token:steel",
		"{country}.market_leader:price@token:textiles",
		"{country}.market_leader:price@token:consumer_goods",
	])
