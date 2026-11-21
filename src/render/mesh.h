// A static GPU mesh: position, normal and colour per vertex, 32-bit indices, drawn as triangles.
#pragma once
#include <cstdint>
#include <vector>

#include "core/math.h"
#include "render/gl.h"

namespace amr {

struct Vertex {
    float x, y, z;
    float nx, ny, nz;
    std::uint8_t r, g, b, a;
};

struct MeshBuilder {
    std::vector<Vertex> verts;
    std::vector<std::uint32_t> idx;
    std::uint32_t add(Vec3 p, Vec3 n, const std::uint8_t rgb[3]) {
        verts.push_back({p.x, p.y, p.z, n.x, n.y, n.z, rgb[0], rgb[1], rgb[2], 255});
        return static_cast<std::uint32_t>(verts.size() - 1);
    }
    void tri(std::uint32_t a, std::uint32_t b, std::uint32_t c) { idx.insert(idx.end(), {a, b, c}); }
    // A flat-shaded quad a-b-c-d (counter-clockwise seen from its front), normal from its corners.
    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, const std::uint8_t rgb[3]) {
        const Vec3 n = normalize(cross(b - a, d - a));
        const auto i = add(a, n, rgb), j = add(b, n, rgb), k = add(c, n, rgb), l = add(d, n, rgb);
        tri(i, j, k);
        tri(i, k, l);
    }
    bool empty() const { return idx.empty(); }
};

class Mesh {
public:
    Mesh() = default;
    explicit Mesh(const MeshBuilder& b) { upload(b); }
    ~Mesh() { release(); }
    Mesh(Mesh&& o) noexcept { *this = std::move(o); }
    Mesh& operator=(Mesh&& o) noexcept {
        if (this != &o) {
            release();
            vao_ = o.vao_; vbo_ = o.vbo_; ibo_ = o.ibo_; count_ = o.count_;
            o.vao_ = o.vbo_ = o.ibo_ = 0;
            o.count_ = 0;
        }
        return *this;
    }
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void upload(const MeshBuilder& b);
    void draw() const;
    GLsizei count() const { return count_; }

private:
    void release();
    GLuint vao_ = 0, vbo_ = 0, ibo_ = 0;
    GLsizei count_ = 0;
};

}  // namespace amr
