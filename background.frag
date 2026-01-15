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

// hash
float hash(float n) {
	return fract(sin(n) * 920834.4273498);
}

float hash(vec2 p) {
	return fract(sin(dot(p, vec2(12789.1, 829.7867))) * 98034.323);
}

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

	int scale = 255;
	int x = int(position.x * scale);	// 0 - 255
	int y = int(position.y * scale);	// 0 - 255

	int bitX = x & 15;
	int bitY = y & 240;

	float colorX = float(bitX) / 255.0;
	float colorY = float(bitY) / 255.0;



	float slice = floor(position.y * 40.0);     // number of horizontal bands
	float rnd   = hash(slice + floor(pushData.time * 10.0));

	float glitchStrength = step(0.85, rnd);     // occasional glitch
	float offset = glitchStrength * (rnd - 0.5) * 0.1;

	vec2 glitchPos = position;
	// glitchPos.x += offset;

	vec2 block = floor(position * vec2(20.0, 12.0));
	float blockNoise = hash(block + floor(pushData.time * 5.0));

	float blockGlitch = step(0.9, blockNoise);
	vec2 blockOffset = vec2(
		(hash(block + 1.0) - 0.5) * 0.2,
		0.0
	);

	glitchPos += blockOffset * blockGlitch;
	

	outColor = vec4(
				glitchPos,
				abs(sin(0.5 + pushData.time)),
				1.0);



}	// end of main