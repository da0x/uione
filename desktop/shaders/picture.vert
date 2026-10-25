// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A picture: one instance a part of it, placed in pixels.
#version 450

layout(location = 0) in vec4 bounds;    // x, y, width, height, in pixels
layout(location = 1) in vec4 source;    // u0, v0, u1, v1 of the picture: the part shown
layout(location = 2) in vec4 tint;      // how much of it shows, premultiplied
layout(location = 3) in vec4 clip;

layout(push_constant) uniform frame { vec2 viewport; } screen;

layout(location = 0) out vec2 uv;
layout(location = 1) flat out vec4 color;
layout(location = 2) flat out vec4 clip_out;

const vec2 corners[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(1, 0), vec2(1, 1), vec2(0, 1));

void main() {
	vec2 corner = corners[gl_VertexIndex];
	vec2 at = bounds.xy + corner * bounds.zw;
	uv = mix(source.xy, source.zw, corner);
	color = tint;
	clip_out = clip;
	gl_Position = vec4(at / screen.viewport * 2.0 - 1.0, 0.0, 1.0);
}
