#include "render/mesh.h"
#include <utility>

namespace mesh3d {

Mesh::~Mesh() {
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
}

Mesh::Mesh(Mesh&& o) noexcept
    : m_vao(o.m_vao), m_vbo(o.m_vbo), m_ebo(o.m_ebo),
      m_count(o.m_count), m_indexed(o.m_indexed), m_idx_type(o.m_idx_type)
{
    o.m_vao = o.m_vbo = o.m_ebo = 0;
}

Mesh& Mesh::operator=(Mesh&& o) noexcept {
    if (this != &o) {
        if (m_vao) glDeleteVertexArrays(1, &m_vao);
        if (m_vbo) glDeleteBuffers(1, &m_vbo);
        if (m_ebo) glDeleteBuffers(1, &m_ebo);
        m_vao = o.m_vao; m_vbo = o.m_vbo; m_ebo = o.m_ebo;
        m_count = o.m_count; m_indexed = o.m_indexed; m_idx_type = o.m_idx_type;
        o.m_vao = o.m_vbo = o.m_ebo = 0;
    }
    return *this;
}

void Mesh::upload(const void* vertices, size_t vert_bytes,
                  const std::vector<Attrib>& attribs,
                  const void* indices, size_t idx_bytes, GLenum idx_type) {
    if (m_vao) {
        glDeleteVertexArrays(1, &m_vao);
        glDeleteBuffers(1, &m_vbo);
        if (m_ebo) glDeleteBuffers(1, &m_ebo);
        m_ebo = 0;
    }

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, vert_bytes, vertices, GL_STATIC_DRAW);

    for (auto& a : attribs) {
        glEnableVertexAttribArray(a.index);
        glVertexAttribPointer(a.index, a.size, a.type, GL_FALSE, a.stride,
                              reinterpret_cast<const void*>(a.offset));
    }

    if (indices && idx_bytes > 0) {
        glGenBuffers(1, &m_ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx_bytes, indices, GL_STATIC_DRAW);
        m_indexed = true;
        m_idx_type = idx_type;
        size_t elem_size = (idx_type == GL_UNSIGNED_SHORT) ? 2 : 4;
        m_count = static_cast<int>(idx_bytes / elem_size);
    } else {
        m_indexed = false;
    }

    glBindVertexArray(0);
}

void Mesh::draw(GLenum mode) const {
    if (!m_vao || m_count == 0) return;
    glBindVertexArray(m_vao);
    if (m_indexed)
        glDrawElements(mode, m_count, m_idx_type, nullptr);
    else
        glDrawArrays(mode, 0, m_count);
    glBindVertexArray(0);
}

void Mesh::draw_instanced(int count, GLenum mode) const {
    if (!m_vao || m_count == 0) return;
    glBindVertexArray(m_vao);
    if (m_indexed)
        glDrawElementsInstanced(mode, m_count, m_idx_type, nullptr, count);
    else
        glDrawArraysInstanced(mode, 0, m_count, count);
    glBindVertexArray(0);
}

} // namespace mesh3d
