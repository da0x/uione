// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A picture's color, read in linear light from its sRGB texture, smoothly between
// its smaller copies when it's shown smaller, and premultiplied to blend; outside its
// rounded corners, nothing, smoothly at their edge.
#version 450

layout(location = 0) in vec2 uv;
layout(location = 1) flat in vec4 color;
layout(location = 2) flat in vec4 clip;
layout(location = 3) flat in vec4 rounded;
layout(location = 4) flat in float radius;

layout(set = 0, binding = 0) uniform sampler2D picture;

layout(location = 0) out vec4 result;

void main() {
	if (gl_FragCoord.x < clip.x || gl_FragCoord.y < clip.y || gl_FragCoord.x > clip.z || gl_FragCoord.y > clip.w) discard;
	vec4 seen = texture(picture, uv);
	result = vec4(seen.rgb * seen.a, seen.a) * color;
	if (radius > 0.0) {
		vec2 half_size = rounded.zw / 2.0;
		vec2 q = abs(gl_FragCoord.xy - rounded.xy - half_size) - half_size + radius;
		float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
		result *= clamp(0.5 - d, 0.0, 1.0);
	}
}
