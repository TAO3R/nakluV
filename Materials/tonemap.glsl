vec3 apply_tone_map(vec3 radiance, float exposure_scale, uint tone_map_mode) {
    vec3 c = radiance * exposure_scale;

    if (tone_map_mode == 1u) {
        // Reinhard operator
        c = c / (1.0 + c);
    }
    // mode 0 (linear): pass through unchanged

    return c;
}
