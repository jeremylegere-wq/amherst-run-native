#include "render/mesh.h"

#include <cstddef>

namespace amr {

void Mesh::upload(const MeshBuilder& b) {
    release();
    if (b.idx.empty()) return;
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(b.verts.size() * sizeof(Vertex)), b.verts.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &ibo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(b.idx.size() * sizeof(std::uint32_t)), b.idx.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, nx)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, r)));
    glBindVertexArray(0);
    count_ = static_cast<GLsizei>(b.idx.size());
}

void Mesh::draw() const {
    if (!count_) return;
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, count_, GL_UNSIGNED_INT, nullptr);
}

void Mesh::release() {
    if (ibo_) glDeleteBuffers(1, &ibo_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    vao_ = vbo_ = ibo_ = 0;
    count_ = 0;
}

}  // namespace amr
