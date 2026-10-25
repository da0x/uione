// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A picture's color, read in linear light from its sRGB texture, smoothly between
// its smaller copies when it's shown smaller, and premultiplied to blend.
#version 450

layout(location = 0) in vec2 uv;
layout(location = 1) flat in vec4 color;
layout(location = 2) flat in vec4 clip;

layout(set = 0, binding = 0) uniform sampler2D picture;

layout(location = 0) out vec4 result;

void main() {
	if (gl_FragCoord.x < clip.x || gl_FragCoord.y < clip.y || gl_FragCoord.x > clip.z || gl_FragCoord.y > clip.w) discard;
	vec4 seen = texture(picture, uv);
	result = vec4(seen.rgb * seen.a, seen.a) * color;
}
