#pragma once
#include <glad/glad.h>
#include <string>
#include <glm/glm.hpp>

namespace mesh3d {

class ComputeShader {
public:
    ComputeShader() = default;
    ~ComputeShader();

    bool load(const std::string& comp_path);
    bool load_source(const char* comp_src);
    void use() const;
    GLuint id() const { return m_program; }

    void dispatch(GLuint groups_x, GLuint groups_y, GLuint groups_z) const;

    /* Uniform setters */
    void set_int(const char* name, int v) const;
    void set_ivec2(const char* name, int x, int y) const;
    void set_float(const char* name, float v) const;
    void set_vec3(const char* name, const glm::vec3& v) const;

    ComputeShader(const ComputeShader&) = delete;
    ComputeShader& operator=(const ComputeShader&) = delete;
    ComputeShader(ComputeShader&& o) noexcept;
    ComputeShader& operator=(ComputeShader&& o) noexcept;

private:
    GLuint m_program = 0;
};

} // namespace mesh3d
