/* Copyright (c) 2018-2024, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */

#version 450
#extension GL_GOOGLE_include_directive : require

layout (location = 0) in vec3 inUVW;
layout (location = 0) out vec4 outColor;

layout (binding = 2) uniform samplerCube samplerEnv;

void main() 
{
	outColor = vec4(textureLod(samplerEnv, inUVW, 1.5).rgb, 1.0);
}
