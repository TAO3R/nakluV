#version 450 //GLSL version 4.5

// no built-in output -- have to declare our color output and write it during main in order to color the fragment
layout(location = 0) out vec4 outColor;

//	gl_FragCoord
//		.x -> pixel X coordinate
//		.y -> pixel Y coordinate
//		.z -> depth
//		.w -> 1 / clip-space W
//		coordinates are in window space (origin at top-left corner)

layout(location = 0) in vec2 position;

layout(push_constant) uniform Push {
	float time;
} pushData;

void main() {
	// sets 'outColor' as a function of 'gl_FragCoord'
	// step based
	//	outColor = vec4(
	//		fract(gl_FragCoord.x / 100),	// r, ramps from 0 -> 1 every 100 pixels
	//		gl_FragCoord.y / 400,			// g, ramps from 0 -> 1 from y = 0 to y = 400 (beyond 1 are clamped by framebuffer format)
	//		0.2,							// b, always 0.2
	//		1.0								// a, always 1.0 (opaque)
	//	);

	// wavy
	//	outColor = vec4(
	//		0.5 + 0.5 * sin(gl_FragCoord.x * 0.01),
	//		0.5 + 0.5 * sin(gl_FragCoord.y * 0.01),	
	//		0.5,
	//		1.0							
	//	);

	// relies on barycentric coordinates to interpolate values between vertices to their values at fragments
	int scale = 255;
	int x = int(position.x * scale);	// 0 - 255
	int y = int(position.y * scale);	// 0 - 255

	int bitX = x & 31;
	int bitY = y & 224;

	float colorX = float(bitX) / 255.0;
	float colorY = float(bitY) / 255.0;

	outColor = vec4(
				fract(colorX + pushData.time),
				colorY,
				0.5,
				1.0);

}	// end of main