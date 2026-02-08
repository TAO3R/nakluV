#pragma once

// A *samll* matrix math library for 4x4 matrices only.

#include <array>
#include <cmath>
#include <cstdint>

// NOTE: column-major storage order (like in OpenGGL / GLSL):
using mat4 = std::array<float, 16>;
static_assert(sizeof(mat4) == 16*4, "mat4 is exactly 16 32-bit floats.");

using vec4 = std::array<float, 4>;
static_assert(sizeof(vec4) == 4*4, "vec4 is exactly 4 32-bit floats.");

// matrix-vector multiplication
inline vec4 operator*(mat4 const &A, vec4 const &b) {
    vec4 ret;
    // compute ret = A * b:
    for (uint32_t r = 0; r < 4; r++) {
        ret[r] = A[0 * 4 + r] * b[0];
        for (uint32_t k = 1; k < 4; k++) {
            ret[r] += A[k * 4 + r] * b[k];
        }
    }

    return ret;
}

// matrix-matrix multiplication
inline mat4 operator*(mat4 const &A, mat4 const &B) {
    mat4 ret;
    // compute ret = A * B:
    for (uint32_t c = 0; c < 4; c++) {
        for (uint32_t r = 0; r < 4; r++) {
            ret[c * 4 + r] = A[0 * 4 + r] * B[c * 4 + 0];
            for (uint32_t k = 1; k < 4; k++) {
                ret[c * 4 + r] += A[k * 4 + r] * B[c * 4 + k];
            }
        }
    }

    return ret;
}

// identity mat4
inline mat4 mat4_identity() {
    return mat4{    // column-major
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

// returns the translation matrix given a vec3 position
inline mat4 mat4_translation( float x, float y, float z) {
    return mat4{    // column-major
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        x,    y   , z,    1.0f,
    };
}

// returns the rotation matrix given a vec4 quaternion
inline mat4 mat4_rotation(float x, float y, float z, float w) {
    return mat4{    // column-major
        1 - 2*(y*y + z*z),  2*(x*y + z*w),      2*(x*z - y*w),      0.0f,
        2*(x*y - z*w),      1 - 2*(x*x + z*z),  2*(y*z + x*w),      0.0f,
        2*(x*z + y*w),      2*(y*z - x*w),      1 - 2*(x*x + y*y),  0.0f,
        0.0f,               0.0f,               0.0f,               1.0f,
    };
}

// returns the scaling matrix given a vec3 scale
inline mat4 mat4_scale(float x, float y, float z) {
    return mat4{    // column-major
        x,    0.0f, 0.0f, 0.0f,
        0.0f, y,    0.0f, 0.0f,
        0.0f, 0.0f, z,    0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

// computes the inverse transpose of a 4x4 matrix.
// used for transforming normals correctly under non-uniform scale.
// only the upper-left 3x3 matters for normals; the 4th row/col stays as identity.
inline mat4 mat4_inverse_transpose(mat4 const &m) {
    // Use shorthand for the upper-left 3x3 elements (column-major):
    // Column 0: m[0], m[1], m[2]
    // Column 1: m[4], m[5], m[6]
    // Column 2: m[8], m[9], m[10]
    float a00 = m[0], a01 = m[4], a02 = m[8];
    float a10 = m[1], a11 = m[5], a12 = m[9];
    float a20 = m[2], a21 = m[6], a22 = m[10];

    // Cofactors of the 3x3 submatrix:
    float c00 =  (a11 * a22 - a12 * a21);
    float c01 = -(a10 * a22 - a12 * a20);
    float c02 =  (a10 * a21 - a11 * a20);

    float c10 = -(a01 * a22 - a02 * a21);
    float c11 =  (a00 * a22 - a02 * a20);
    float c12 = -(a00 * a21 - a01 * a20);

    float c20 =  (a01 * a12 - a02 * a11);
    float c21 = -(a00 * a12 - a02 * a10);
    float c22 =  (a00 * a11 - a01 * a10);

    // Determinant via first row expansion:
    float det = a00 * c00 + a01 * c01 + a02 * c02;
    float inv_det = 1.0f / det;

    // inverse = cofactor matrix / det
    // inverse transpose = transpose of (cofactor matrix / det)
    //                   = (cofactor matrix)^T / det
    // in column-major, storing the transpose means swapping rows and columns:
    return mat4{
        c00 * inv_det, c10 * inv_det, c20 * inv_det, 0.0f,
        c01 * inv_det, c11 * inv_det, c21 * inv_det, 0.0f,
        c02 * inv_det, c12 * inv_det, c22 * inv_det, 0.0f,
        0.0f,          0.0f,          0.0f,          1.0f,
    };
}

// General 4x4 matrix inverse using cofactor expansion.
// Returns the inverse of m. Behaviour is undefined if m is singular (det == 0).
// Column-major: element at row r, column c is m[c*4 + r].
inline mat4 mat4_inverse(mat4 const &m) {
    // 2x2 sub-determinants from rows 0,1:
    float s0 = m[0]*m[5]  - m[4]*m[1];
    float s1 = m[0]*m[9]  - m[8]*m[1];
    float s2 = m[0]*m[13] - m[12]*m[1];
    float s3 = m[4]*m[9]  - m[8]*m[5];
    float s4 = m[4]*m[13] - m[12]*m[5];
    float s5 = m[8]*m[13] - m[12]*m[9];

    // 2x2 sub-determinants from rows 2,3:
    float c0 = m[2]*m[7]   - m[6]*m[3];
    float c1 = m[2]*m[11]  - m[10]*m[3];
    float c2 = m[2]*m[15]  - m[14]*m[3];
    float c3 = m[6]*m[11]  - m[10]*m[7];
    float c4 = m[6]*m[15]  - m[14]*m[7];
    float c5 = m[10]*m[15] - m[14]*m[11];

    float det = s0*c5 - s1*c4 + s2*c3 + s3*c2 - s4*c1 + s5*c0;
    float inv_det = 1.0f / det;

    // adjugate / det, stored column-major: result[col*4 + row] = adj[row][col] / det
    return mat4{
        // column 0
        ( m[5]*c5  - m[9]*c4  + m[13]*c3) * inv_det,
        (-m[1]*c5  + m[9]*c2  - m[13]*c1) * inv_det,
        ( m[1]*c4  - m[5]*c2  + m[13]*c0) * inv_det,
        (-m[1]*c3  + m[5]*c1  - m[9]*c0)  * inv_det,
        // column 1
        (-m[4]*c5  + m[8]*c4  - m[12]*c3) * inv_det,
        ( m[0]*c5  - m[8]*c2  + m[12]*c1) * inv_det,
        (-m[0]*c4  + m[4]*c2  - m[12]*c0) * inv_det,
        ( m[0]*c3  - m[4]*c1  + m[8]*c0)  * inv_det,
        // column 2
        ( m[7]*s5  - m[11]*s4 + m[15]*s3) * inv_det,
        (-m[3]*s5  + m[11]*s2 - m[15]*s1) * inv_det,
        ( m[3]*s4  - m[7]*s2  + m[15]*s0) * inv_det,
        (-m[3]*s3  + m[7]*s1  - m[11]*s0) * inv_det,
        // column 3
        (-m[6]*s5  + m[10]*s4 - m[14]*s3) * inv_det,
        ( m[2]*s5  - m[10]*s2 + m[14]*s1) * inv_det,
        (-m[2]*s4  + m[6]*s2  - m[14]*s0) * inv_det,
        ( m[2]*s3  - m[6]*s1  + m[10]*s0) * inv_det,
    };
}

// perspective projection matrix
// - vfov is fov *in radians*
// - near maps to 0, far maps to 1
// looks down -z with +y up and +x right
inline mat4 perspective(float vfov, float aspect, float near, float far) {
    // as per https://www.terathon.com/gdc07_lengyel.pdf
    //  (with modifications for Vulkan-style coordinate system)
    //  notably: flip y (vulkan device coords are y-down)
    //      and rescale z (vulkan device coords are z-[0, 1])
    const float e = 1.0f / std::tan(vfov / 2.0f);
    const float a = aspect;
    const float n = near;
    const float f = far;
    return mat4 {   // note: column-major storage order
        e/a,    0.0f,   0.0f,                           0.0f,
        0.0f,   -e,     0.0f,                           0.0f,
        0.0f,   0.0f,   -0.5f - 0.5f * (f+n)/(f-n),     -1.0f,
        0.0f,   0.0f,   -(f*n)/(f-n),                   0.0f,
    };
}

// look at matrix:
// makes a camera-space-from-world matrix for a camera at eye looking toward
// target with up-vector pointing (as-close-as-possible) along up.
// That is, it maps:
//  - eye xyz to the origin
//  - the unit length vector from eye_xyz to target_xyz to -z
//  - an as-close-as-possible unit-length vector to up to +y
inline mat4 look_at(
    float eye_x, float eye_y, float eye_z,
    float target_x, float target_y, float target_z,
    float up_x, float up_y, float up_z) {
    
        // NOTE: this would be a lot cleaner with a vec3 type and some overloads!

        // compute vector from eye to target:
        float in_x = target_x - eye_x;
        float in_y = target_y - eye_y;
        float in_z = target_z - eye_z;
        
        // normalize 'in' vector:
        float inv_in_len = 1.0f / std::sqrt(in_x*in_x + in_y*in_y + in_z*in_z);
        in_x *= inv_in_len;
        in_y *= inv_in_len;
        in_z *= inv_in_len;

        // make 'up' orthogonal to 'in'
        float in_dot_up = in_x*up_x + in_y*up_y + in_z*up_z;
        up_x -= in_dot_up * in_x;
        up_y -= in_dot_up * in_y;
        up_z -= in_dot_up * in_z;
        

        // normalize 'up' vector
        float inv_up_len = 1.0f / std::sqrt(up_x*up_x + up_y*up_y + up_z*up_z);
        up_x *= inv_up_len;
        up_y *= inv_up_len;
        up_z *= inv_up_len;

        // compute 'right' vector as 'in' x 'up'
        float right_x = in_y*up_z - in_z*up_y;
        float right_y = in_z*up_x - in_x*up_z;
        float right_z = in_x*up_y - in_y*up_x;

        // compute dot products of right, in, up with eye:
        float right_dot_eye = right_x*eye_x + right_y*eye_y + right_z*eye_z;
        float up_dot_eye = up_x*eye_x + up_y*eye_y + up_z*eye_z;
        float in_dot_eye = in_x*eye_x + in_y*eye_y + in_z*eye_z;

        // final matrix: (computes (right . (v - eye), up . (v - eye), -in . (v - eye), v . w)
        return mat4{    // note: column-major storage order
            right_x,            up_x,           -in_x,          0.0f,
            right_y,            up_y,           -in_y,          0.0f,
            right_z,            up_z,           -in_z,          0.0f,
            -right_dot_eye,     -up_dot_eye,    in_dot_eye,     1.0f
        };
}

// orbit camera matrix:
// makes a camera-from-world matrix for a camera orbiting target {x, y, z}
// at distance radius with angles azimuth and elevation.
// azimuth is counterclockwise angle in the xy plane from the x axis
// elevation is angle up from the xy plane
// both are in radius
inline mat4 orbit(
    float target_x, float target_y, float target_z,
    float azimuth, float elevation, float radius
) {
    // shorthand for some useful trig values:
    float ca = std::cos(azimuth);
    float sa = std::sin(azimuth);
    float ce = std::cos(elevation);
    float se = std::sin(elevation);
    
    // camera's right direction is azimuth rotated by 90 degrees:
    float right_x = -sa;
    float right_y = ca;
    float right_z = 0.0f;   // the vector is parallel to the xy plane, so its z component must be zero
    
    // camera's up direction is elevation rotated 90 degrees:
    // (and points in the same xy direction as azimuth)
    float up_x = -se * ca;
    float up_y = -se * sa;
    float up_z = ce;

    // direction to the camera from the target,
    // which is the azimuth direction rotated up by the elevation angle:
    float out_x = ce * ca;
    float out_y = ce * sa;
    float out_z = se;

    // camera position
    float eye_x = target_x + radius * out_x;
    float eye_y = target_y + radius * out_y;
    float eye_z = target_z + radius * out_z;

    // camera's position projected onto the various vectors:
    float right_dot_eye = right_x * eye_x + right_y * eye_y + right_z * eye_z;
    float up_dot_eye = up_x * eye_x + up_y * eye_y + up_z * eye_z;
    float out_dot_eye = out_x * eye_x + out_y * eye_y + out_z * eye_z;

    // the final local-from-world transformation (column-major)
    return mat4{
        right_x, up_x, out_x, 0.0f,
        right_y, up_y, out_y, 0.0f,
        right_z, up_z, out_z, 0.0f,
        -right_dot_eye, -up_dot_eye, -out_dot_eye, 1.0f,
    };
}