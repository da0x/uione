// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// A box: one instance a rectangle, drawn as two triangles grown by its blur, so a
// shadow's soft edge has room.
#version 450

layout(location = 0) in vec4 bounds;    // x, y, width, height, in pixels
layout(location = 1) in vec4 fill;      // premultiplied, linear
layout(location = 2) in vec4 edge;      // the border's color, premultiplied
layout(location = 3) in vec4 shape;     // radius, border width, blur, unused
layout(location = 4) in vec4 clip;      // x0, y0, x1, y1

layout(push_constant) uniform frame { vec2 viewport; } screen;

layout(location = 0) out vec2 local;    // from the box's center, in pixels
layout(location = 1) flat out vec2 half_size;
layout(location = 2) flat out vec4 fill_color;
layout(location = 3) flat out vec4 edge_color;
layout(location = 4) flat out vec4 shape_out;
layout(location = 5) flat out vec4 clip_out;

const vec2 corners[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(1, 0), vec2(1, 1), vec2(0, 1));

void main() {
	float grow = shape.z * 1.5 + 1.0;
	vec2 corner = corners[gl_VertexIndex];
	vec2 at = bounds.xy - grow + corner * (bounds.zw + 2.0 * grow);
	half_size = bounds.zw * 0.5;
	local = at - (bounds.xy + half_size);
	fill_color = fill;
	edge_color = edge;
	shape_out = shape;
	clip_out = clip;
	gl_Position = vec4(at / screen.viewport * 2.0 - 1.0, 0.0, 1.0);
}
