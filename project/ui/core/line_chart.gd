class_name LineChart
extends Control
## Minimal multi-series line chart. set_series({name: PackedFloat64Array}) and it redraws.
## NaN samples (eval errors / not yet watched) are skipped.

const COLORS := [
	Color(0.36, 0.67, 0.93), Color(0.95, 0.6, 0.25), Color(0.45, 0.8, 0.45), Color(0.9, 0.4, 0.45),
	Color(0.7, 0.55, 0.9), Color(0.85, 0.8, 0.35), Color(0.4, 0.8, 0.8), Color(0.75, 0.75, 0.75),
]

var _series: Dictionary = {}


func set_series(series: Dictionary) -> void:
	_series = series
	queue_redraw()


func _draw() -> void:
	var font := get_theme_default_font()
	var rect := Rect2(Vector2(60, 8), size - Vector2(70, 40))
	draw_rect(Rect2(Vector2.ZERO, size), Color(0.1, 0.11, 0.13))
	draw_rect(rect, Color(0.3, 0.3, 0.35), false)
	var lo := INF
	var hi := -INF
	var n := 0
	for k in _series:
		var arr: PackedFloat64Array = _series[k]
		n = maxi(n, arr.size())
		for v in arr:
			if not is_nan(v):
				lo = minf(lo, v)
				hi = maxf(hi, v)
	if n < 2 or lo == INF:
		draw_string(font, rect.position + Vector2(8, 20), "no data yet — run the simulation", HORIZONTAL_ALIGNMENT_LEFT, -1, 14, Color(0.6, 0.6, 0.6))
		return
	if is_equal_approx(lo, hi):
		hi = lo + 1.0
	draw_string(font, Vector2(2, rect.position.y + 12), PanelBuilder.fmt(hi), HORIZONTAL_ALIGNMENT_LEFT, 56, 12, Color(0.7, 0.7, 0.7))
	draw_string(font, Vector2(2, rect.end.y), PanelBuilder.fmt(lo), HORIZONTAL_ALIGNMENT_LEFT, 56, 12, Color(0.7, 0.7, 0.7))
	if lo < 0.0 and hi > 0.0:
		var zy := rect.end.y - (0.0 - lo) / (hi - lo) * rect.size.y
		draw_line(Vector2(rect.position.x, zy), Vector2(rect.end.x, zy), Color(0.35, 0.35, 0.4), 1.0)
	var idx := 0
	var legend_x := rect.position.x
	for k in _series:
		var arr: PackedFloat64Array = _series[k]
		var col: Color = COLORS[idx % COLORS.size()]
		var prev := Vector2.INF
		for i in arr.size():
			var v := arr[i]
			if is_nan(v):
				prev = Vector2.INF
				continue
			var p := Vector2(rect.position.x + rect.size.x * float(i) / float(maxi(n - 1, 1)),
				rect.end.y - (v - lo) / (hi - lo) * rect.size.y)
			if prev != Vector2.INF:
				draw_line(prev, p, col, 2.0, true)
			prev = p
		draw_string(font, Vector2(legend_x, size.y - 8), str(k), HORIZONTAL_ALIGNMENT_LEFT, -1, 12, col)
		legend_x += font.get_string_size(str(k), HORIZONTAL_ALIGNMENT_LEFT, -1, 12).x + 18
		idx += 1
