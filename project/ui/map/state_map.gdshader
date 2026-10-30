shader_type canvas_item;
// State map (adapted from Rota Fortunae's map.gdshader): every pixel of TEXTURE encodes a dense state index
// (r + g<<8 + b<<16). Its colour is read from a 1-pixel-tall palette row, so switching map modes only
// rewrites the palette. Index 0 = sea / not loaded.

uniform sampler2D palette_tex : filter_nearest;
uniform int selected_id = -1;
uniform int hovered_id = -1;

int decode_id(vec4 enc) {
	return int(enc.r * 255.0 + 0.5) + (int(enc.g * 255.0 + 0.5) << 8) + (int(enc.b * 255.0 + 0.5) << 16);
}

void fragment() {
	int id = decode_id(texture(TEXTURE, UV));
	vec4 col = vec4(0.07, 0.09, 0.13, 1.0);
	if (id > 0) {
		col = texelFetch(palette_tex, ivec2(id, 0), 0);
		vec2 px = TEXTURE_PIXEL_SIZE;
		int right = decode_id(texture(TEXTURE, UV + vec2(px.x, 0.0)));
		int down = decode_id(texture(TEXTURE, UV + vec2(0.0, px.y)));
		if (right != id || down != id) {
			col.rgb *= 0.55; // state borders
		}
		if (id == selected_id) {
			col.rgb = mix(col.rgb, vec3(1.0), 0.45);
		} else if (id == hovered_id) {
			col.rgb = mix(col.rgb, vec3(1.0, 1.0, 0.7), 0.3);
		}
	}
	COLOR = col;
}
