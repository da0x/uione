// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A box's color at a pixel, from its rounded rectangle's signed distance: its fill,
// its border inside the edge, or, blurred, a shadow falling off as a Gaussian does.
// A line, as an icon is drawn, is a segment with round ends: its ends where the
// border's color would be, its half width where the border's would be.
#version 450

layout(location = 0) in vec2 local;
layout(location = 1) flat in vec2 half_size;
layout(location = 2) flat in vec4 fill_color;
layout(location = 3) flat in vec4 edge_color;
layout(location = 4) flat in vec4 shape;
layout(location = 5) flat in vec4 clip;

layout(location = 0) out vec4 color;

float rounded_box(vec2 p, vec2 half_extent, float radius) {
	vec2 q = abs(p) - half_extent + radius;
	return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

// erf, to within 0.0005 (Abramowitz and Stegun 7.1.27).
float erf_approx(float x) {
	float s = sign(x), a = abs(x);
	float t = 1.0 + (0.278393 + (0.230389 + (0.000972 + 0.078108 * a) * a) * a) * a;
	t *= t;
	return s - s / (t * t);
}

float segment(vec2 p, vec2 a, vec2 b) {
	vec2 pa = p - a, ba = b - a;
	float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
	return length(pa - ba * h);
}

void main() {
	if (gl_FragCoord.x < clip.x || gl_FragCoord.y < clip.y || gl_FragCoord.x > clip.z || gl_FragCoord.y > clip.w) discard;
	if (shape.w > 0.5) {
		float d = segment(local, edge_color.xy, edge_color.zw) - shape.y;
		color = fill_color * clamp(0.5 - d, 0.0, 1.0);
		if (color.a <= 0.0) discard;
		return;
	}
	float radius = min(shape.x, min(half_size.x, half_size.y));
	float d = rounded_box(local, half_size, radius);
	float blur = shape.z;
	if (blur > 0.0) {
		float sigma = blur * 0.5;
		float coverage = 0.5 - 0.5 * erf_approx(d / (sigma * 1.41421356));
		color = fill_color * coverage;
		return;
	}
	float outside = clamp(0.5 - d, 0.0, 1.0);
	float inside = clamp(0.5 - (d + shape.y), 0.0, 1.0);
	color = fill_color * inside + edge_color * (outside - inside);
	if (color.a <= 0.0) discard;
}
