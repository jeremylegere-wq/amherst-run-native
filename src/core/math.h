// Small vector and matrix maths: all the game needs, so no maths library.
// Conventions: right-handed, x east, y north, z up (the same as the Python game's world), column-major 4x4
// matrices as OpenGL expects, angles in radians.
#pragma once
#include <cmath>

namespace amr {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
    float l = length(a);
    return l > 0 ? a * (1.0f / l) : a;
}

struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // column-major: m[col * 4 + row]
    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.at(row, k) * b.at(k, c);
            r.at(row, c) = s;
        }
    return r;
}

inline Vec3 transform_point(const Mat4& a, Vec3 p) {  // (with the perspective divide)
    float x = a.at(0, 0) * p.x + a.at(0, 1) * p.y + a.at(0, 2) * p.z + a.at(0, 3);
    float y = a.at(1, 0) * p.x + a.at(1, 1) * p.y + a.at(1, 2) * p.z + a.at(1, 3);
    float z = a.at(2, 0) * p.x + a.at(2, 1) * p.y + a.at(2, 2) * p.z + a.at(2, 3);
    float w = a.at(3, 0) * p.x + a.at(3, 1) * p.y + a.at(3, 2) * p.z + a.at(3, 3);
    return w != 0 ? Vec3{x / w, y / w, z / w} : Vec3{x, y, z};
}

inline Mat4 translate(Vec3 t) {
    Mat4 r;
    r.at(0, 3) = t.x;
    r.at(1, 3) = t.y;
    r.at(2, 3) = t.z;
    return r;
}

inline Mat4 rotate_z(float a) {  // yaw: turning on the map
    Mat4 r;
    float c = std::cos(a), s = std::sin(a);
    r.at(0, 0) = c;
    r.at(0, 1) = -s;
    r.at(1, 0) = s;
    r.at(1, 1) = c;
    return r;
}

// Perspective projection, vertical field of view fovy, OpenGL clip space (z -1..1).
inline Mat4 perspective(float fovy, float aspect, float znear, float zfar) {
    Mat4 r;
    float f = 1.0f / std::tan(fovy / 2);
    r.at(0, 0) = f / aspect;
    r.at(1, 1) = f;
    r.at(2, 2) = (zfar + znear) / (znear - zfar);
    r.at(2, 3) = 2 * zfar * znear / (znear - zfar);
    r.at(3, 2) = -1;
    r.at(3, 3) = 0;
    return r;
}

// View matrix: camera at eye looking at target, with z up.
inline Mat4 look_at(Vec3 eye, Vec3 target, Vec3 up = {0, 0, 1}) {
    Vec3 f = normalize(target - eye);
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);
    Mat4 r;
    r.at(0, 0) = s.x; r.at(0, 1) = s.y; r.at(0, 2) = s.z; r.at(0, 3) = -dot(s, eye);
    r.at(1, 0) = u.x; r.at(1, 1) = u.y; r.at(1, 2) = u.z; r.at(1, 3) = -dot(u, eye);
    r.at(2, 0) = -f.x; r.at(2, 1) = -f.y; r.at(2, 2) = -f.z; r.at(2, 3) = dot(f, eye);
    return r;
}

}  // namespace amr
