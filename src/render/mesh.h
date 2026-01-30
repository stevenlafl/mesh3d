#pragma once
#include <glad/glad.h>
#include <vector>
#include <cstddef>

namespace mesh3d {

/* Lightweight VAO/VBO/EBO wrapper. */
class Mesh {
public:
    Mesh() = default;
    ~Mesh();

    struct Attrib {
        GLuint index;
        GLint  size;       // 1,2,3,4
        GLenum type;       // GL_FLOAT, etc.
        GLsizei stride;
        size_t  offset;
    };

    /* Upload vertex data with attribute layout. indices may be empty for non-indexed draws. */
    void upload(const void* vertices, size_t vert_bytes,
                const std::vector<Attrib>& attribs,
                const void* indices = nullptr, size_t idx_bytes = 0,
                GLenum idx_type = GL_UNSIGNED_INT);

    void draw(GLenum mode = GL_TRIANGLES) const;
    void draw_instanced(int count, GLenum mode = GL_TRIANGLES) const;

    GLuint vao() const { return m_vao; }
    bool   valid() const { return m_vao != 0; }
    int    element_count() const { return m_count; }

    void set_vertex_count(int n) { m_count = n; m_indexed = false; }

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& o) noexcept;
    Mesh& operator=(Mesh&& o) noexcept;

private:
    GLuint m_vao = 0, m_vbo = 0, m_ebo = 0;
    int    m_count = 0;
    bool   m_indexed = false;
    GLenum m_idx_type = GL_UNSIGNED_INT;
};

} // namespace mesh3d
