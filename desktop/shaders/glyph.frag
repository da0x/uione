// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// A glyph's coverage from the atlas, in its color. Coverage is lifted a little, as
// blending in linear light thins dark text on a light page otherwise.
#version 450

layout(location = 0) in vec2 uv;
layout(location = 1) flat in vec4 color;
layout(location = 2) flat in vec4 clip;

layout(set = 0, binding = 0) uniform sampler2D atlas;

layout(location = 0) out vec4 result;

void main() {
	if (gl_FragCoord.x < clip.x || gl_FragCoord.y < clip.y || gl_FragCoord.x > clip.z || gl_FragCoord.y > clip.w) discard;
	float coverage = texture(atlas, uv).r;
	coverage = pow(coverage, 1.0 / 1.45);
	if (coverage <= 0.0) discard;
	result = color * coverage;
}
