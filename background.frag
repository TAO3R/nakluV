#version 450 //GLSL version 4.5

// no built-in output -- have to declare our color output and write it during main in order to color the fragment
layout(location = 0) out vec4 outColor;

//	gl_FragCoord
//		.x -> pixel X coordinate
//		.y -> pixel Y coordinate
//		.z -> depth
//		.w -> 1 / clip-space W
//		coordinates are in window space (origin at top-left corner)

void main() {
	// sets 'outColor' as a function of 'gl_FragCoord'
	//	outColor = vec4(
	//		fract(gl_FragCoord.x / 100),	// r, ramps from 0 -> 1 every 100 pixels
	//		gl_FragCoord.y / 400,			// g, ramps from 0 -> 1 from y = 0 to y = 400 (beyond 1 are clamped by framebuffer format)
	//		0.2,							// b, always 0.2
	//		1.0								// a, always 1.0 (opaque)
	//	);

	outColor = vec4(
		0.5 + 0.5 * sin(gl_FragCoord.x * 0.01),
		0.5 + 0.5 * sin(gl_FragCoord.y * 0.01),	
		0.5,
		1.0							
	);

}	// end of main