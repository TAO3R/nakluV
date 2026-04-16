#define _CRT_SECURE_NO_WARNINGS

#define STB_IMAGE_IMPLEMENTATION
#include "../SceneViewer/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct Vec3 {
	float x, y, z;
	Vec3 operator+(Vec3 b) const { return {x + b.x, y + b.y, z + b.z}; }
	Vec3 operator-(Vec3 b) const { return {x - b.x, y - b.y, z - b.z}; }
	Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};

static float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

static float length(Vec3 v) { return std::sqrt(dot(v, v)); }

static Vec3 normalize(Vec3 v) {
	float l = length(v);
	return (l > 1e-12f) ? v * (1.0f / l) : Vec3{0, 0, 0};
}

// ---- Cubemap face conventions (s72 / OpenGL standard) ----
// Face order in vertical strip: +X, -X, +Y, -Y, +Z, -Z

static Vec3 direction_from_face_uv(int face, float u, float v) {
	float s = 2.0f * u - 1.0f;
	float t = 2.0f * v - 1.0f;
	switch (face) {
		case 0: return normalize({ 1.0f,    t,   -s}); // +X
		case 1: return normalize({-1.0f,    t,    s}); // -X
		case 2: return normalize({    s, 1.0f,   -t}); // +Y
		case 3: return normalize({    s,-1.0f,    t}); // -Y
		case 4: return normalize({    s,    t, 1.0f}); // +Z
		case 5: return normalize({   -s,    t,-1.0f}); // -Z
		default: return {0, 0, 0};
	}
}

// ---- RGBE decode / encode ----

static void decode_rgbe(const uint8_t *src, float *dst, size_t pixel_count) {
	for (size_t i = 0; i < pixel_count; ++i) {
		uint8_t r = src[i * 4 + 0];
		uint8_t g = src[i * 4 + 1];
		uint8_t b = src[i * 4 + 2];
		uint8_t e = src[i * 4 + 3];
		if (r == 0 && g == 0 && b == 0 && e == 0) {
			dst[i * 3 + 0] = 0.0f;
			dst[i * 3 + 1] = 0.0f;
			dst[i * 3 + 2] = 0.0f;
		} else {
			float scale = std::ldexp(1.0f, static_cast<int>(e) - 128) / 256.0f;
			dst[i * 3 + 0] = (r + 0.5f) * scale;
			dst[i * 3 + 1] = (g + 0.5f) * scale;
			dst[i * 3 + 2] = (b + 0.5f) * scale;
		}
	}
}

static void encode_rgbe(const float *src, uint8_t *dst, size_t pixel_count) {
	for (size_t i = 0; i < pixel_count; ++i) {
		float r = src[i * 3 + 0];
		float g = src[i * 3 + 1];
		float b = src[i * 3 + 2];

		float max_val = std::max(r, std::max(g, b));
		if (max_val < 1e-32f) {
			dst[i * 4 + 0] = 0;
			dst[i * 4 + 1] = 0;
			dst[i * 4 + 2] = 0;
			dst[i * 4 + 3] = 0;
		} else {
			int exponent;
			float mantissa = std::frexp(max_val, &exponent);
			// mantissa is in [0.5, 1.0), so mantissa * 256 is in [128, 256)
			float scale = mantissa * 256.0f / max_val;
			dst[i * 4 + 0] = static_cast<uint8_t>(std::max(0.0f, r * scale - 0.5f));
			dst[i * 4 + 1] = static_cast<uint8_t>(std::max(0.0f, g * scale - 0.5f));
			dst[i * 4 + 2] = static_cast<uint8_t>(std::max(0.0f, b * scale - 0.5f));
			dst[i * 4 + 3] = static_cast<uint8_t>(exponent + 128);
		}
	}
}

// ---- Lambertian convolution ----
// For each output texel with direction d, integrate:
//   irradiance(d) = (1/pi) * integral_hemisphere L(w) * max(dot(d,w), 0) dw
// Approximate by uniform sampling over all input cubemap texels (treating each as a sample).

static void convolve_lambertian(
	const float *input_hdr, int input_face_size,
	float *output_hdr, int output_face_size)
{
	size_t out_face_pixels = static_cast<size_t>(output_face_size) * output_face_size;
	size_t in_face_pixels = static_cast<size_t>(input_face_size) * input_face_size;

	for (int out_face = 0; out_face < 6; ++out_face) {
		for (int out_y = 0; out_y < output_face_size; ++out_y) {
			for (int out_x = 0; out_x < output_face_size; ++out_x) {
				float u = (out_x + 0.5f) / output_face_size;
				float v = (out_y + 0.5f) / output_face_size;
				Vec3 n = direction_from_face_uv(out_face, u, v);

				float accum_r = 0, accum_g = 0, accum_b = 0;
				float weight_sum = 0;

				// Sum over all input texels
				for (int in_face = 0; in_face < 6; ++in_face) {
					for (int in_y = 0; in_y < input_face_size; ++in_y) {
						for (int in_x = 0; in_x < input_face_size; ++in_x) {
							float iu = (in_x + 0.5f) / input_face_size;
							float iv = (in_y + 0.5f) / input_face_size;
							Vec3 w = direction_from_face_uv(in_face, iu, iv);

							float cos_theta = dot(n, w);
							if (cos_theta <= 0.0f) continue;

							// Solid angle of this input texel (approximation):
							// each face covers 2x2 steradians (total 4*pi for sphere),
							// and a face has face_size^2 texels.
							// More precisely, the solid angle varies by texel position:
							//   dw = 4 / (face_size^2 * (s^2 + t^2 + 1)^(3/2))
							// where s,t are in [-1,1]
							float s = 2.0f * iu - 1.0f;
							float t = 2.0f * iv - 1.0f;
							float d2 = s * s + t * t + 1.0f;
							float texel_solid_angle = 4.0f / (in_face_pixels * d2 * std::sqrt(d2));

							size_t idx = in_face * in_face_pixels + static_cast<size_t>(in_y) * input_face_size + in_x;
							float lr = input_hdr[idx * 3 + 0];
							float lg = input_hdr[idx * 3 + 1];
							float lb = input_hdr[idx * 3 + 2];

							float w_sample = cos_theta * texel_solid_angle;
							accum_r += lr * w_sample;
							accum_g += lg * w_sample;
							accum_b += lb * w_sample;
							weight_sum += w_sample;
						}
					}
				}

				// Normalize: the integral of cos_theta over hemisphere is pi,
				// and irradiance = (1/pi) * integral, so divide by pi.
				// But we already weighted by solid angle, so the sum approximates
				// the integral directly. Just divide by pi.
				size_t out_idx = out_face * out_face_pixels
					+ static_cast<size_t>(out_y) * output_face_size + out_x;
				float inv_pi = 1.0f / static_cast<float>(M_PI);
				output_hdr[out_idx * 3 + 0] = accum_r * inv_pi;
				output_hdr[out_idx * 3 + 1] = accum_g * inv_pi;
				output_hdr[out_idx * 3 + 2] = accum_b * inv_pi;
			}
		}
		std::cout << "  face " << out_face << "/5 done" << std::endl;
	}
}

// ---- Main ----

int main(int argc, char **argv) {
	std::string input_path;
	std::string output_path;
	bool do_lambertian = false;
	int output_size = 16;

	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "--lambertian") {
			do_lambertian = true;
			if (i + 1 < argc) {
				++i;
				output_path = argv[i];
			} else {
				std::cerr << "Error: --lambertian requires an output path." << std::endl;
				return 1;
			}
		} else if (arg == "--size") {
			if (i + 1 < argc) {
				++i;
				output_size = std::stoi(argv[i]);
			} else {
				std::cerr << "Error: --size requires a number." << std::endl;
				return 1;
			}
		} else if (input_path.empty()) {
			input_path = arg;
		} else {
			std::cerr << "Error: unexpected argument '" << arg << "'." << std::endl;
			return 1;
		}
	}

	if (input_path.empty() || !do_lambertian) {
		std::cerr << "Usage: cube in.png --lambertian out.png [--size N]" << std::endl;
		return 1;
	}

	// Load input RGBE cubemap
	int img_w = 0, img_h = 0;
	std::unique_ptr<unsigned char, void(*)(void*)> pixels(
		stbi_load(input_path.c_str(), &img_w, &img_h, nullptr, 4),
		[](void *p) { stbi_image_free(p); }
	);

	if (!pixels) {
		std::cerr << "Error: failed to load '" << input_path << "': "
			<< stbi_failure_reason() << std::endl;
		return 1;
	}

	if (img_w <= 0 || img_h != img_w * 6) {
		std::cerr << "Error: image dimensions (" << img_w << "x" << img_h
			<< ") don't match vertical strip cubemap (w x 6w)." << std::endl;
		return 1;
	}

	int input_face_size = img_w;
	size_t input_total_pixels = static_cast<size_t>(input_face_size) * input_face_size * 6;

	std::cout << "Input: " << input_path << " (" << input_face_size << "x"
		<< input_face_size << " per face)" << std::endl;

	// Decode RGBE to HDR float (RGB, 3 floats per pixel)
	std::vector<float> input_hdr(input_total_pixels * 3);
	decode_rgbe(pixels.get(), input_hdr.data(), input_total_pixels);

	// Convolve
	size_t output_total_pixels = static_cast<size_t>(output_size) * output_size * 6;
	std::vector<float> output_hdr(output_total_pixels * 3, 0.0f);

	std::cout << "Convolving lambertian (" << output_size << "x" << output_size
		<< " per face)..." << std::endl;

	convolve_lambertian(input_hdr.data(), input_face_size,
		output_hdr.data(), output_size);

	// Encode to RGBE
	std::vector<uint8_t> output_rgbe(output_total_pixels * 4);
	encode_rgbe(output_hdr.data(), output_rgbe.data(), output_total_pixels);

	// Write as vertical strip PNG (width = output_size, height = output_size * 6)
	int out_w = output_size;
	int out_h = output_size * 6;
	if (!stbi_write_png(output_path.c_str(), out_w, out_h, 4, output_rgbe.data(), out_w * 4)) {
		std::cerr << "Error: failed to write '" << output_path << "'." << std::endl;
		return 1;
	}

	std::cout << "Wrote: " << output_path << " (" << out_w << "x" << out_h << ")" << std::endl;
	return 0;
}
