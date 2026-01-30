#pragma once
#include <glad/glad.h>
#include <string>
#include <glm/glm.hpp>

namespace mesh3d {

class Shader {
public:
    Shader() = default;
    ~Shader();

    bool load(const std::string& vert_path, const std::string& frag_path);
    bool load_source(const char* vert_src, const char* frag_src);
    void use() const;
    GLuint id() const { return m_program; }

    /* Uniform setters */
    void set_int(const char* name, int v) const;
    void set_float(const char* name, float v) const;
    void set_vec3(const char* name, const glm::vec3& v) const;
    void set_vec4(const char* name, const glm::vec4& v) const;
    void set_mat4(const char* name, const glm::mat4& m) const;

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& o) noexcept;
    Shader& operator=(Shader&& o) noexcept;

private:
    GLuint m_program = 0;
    GLuint compile(GLenum type, const char* src);
};

} // namespace mesh3d
