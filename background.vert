#version 450 // GLSL version 4.5

// varying
layout(location = 0) out vec2 position;

void main() {
	// draws a full-screen triangle without any vertex buffer
	// gl_VertexIndex is the index of the vertex being processed (called within vkCmdDraw)
	vec2 POSITION = vec2(
		2 * (gl_VertexIndex & 2) - 1,			// x: 0, 1, 2 -> -1, -1, 3
		4 * (gl_VertexIndex & 1) - 1			// y: 0, 1, 2 -> -1, 3, -1
	);											// clip-space coordinates (-1, -1), (-1, 3), (3, -1)
	gl_Position = vec4(POSITION, 0.0, 1.0);		// Z = 0 -> on the near plane, W = 1 -> normal clip-space coordinate, no perspective distortion

	// can be interpreted as barycentric coordinates to interpolate values between vertices to their values at fragments
	position = POSITION * 0.5 + 0.5;	// make the screen [0, 1] * [0, 1]
}