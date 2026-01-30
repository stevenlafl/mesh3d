#include "render/shader.h"
#include "util/log.h"
#include <fstream>
#include <sstream>
#include <glm/gtc/type_ptr.hpp>

namespace mesh3d {

Shader::~Shader() {
    if (m_program) glDeleteProgram(m_program);
}

Shader::Shader(Shader&& o) noexcept : m_program(o.m_program) { o.m_program = 0; }
Shader& Shader::operator=(Shader&& o) noexcept {
    if (this != &o) {
        if (m_program) glDeleteProgram(m_program);
        m_program = o.m_program;
        o.m_program = 0;
    }
    return *this;
}

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        LOG_ERROR("Failed to open shader file: %s", path.c_str());
        return {};
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool Shader::load(const std::string& vert_path, const std::string& frag_path) {
    std::string vs = read_file(vert_path);
    std::string fs = read_file(frag_path);
    if (vs.empty() || fs.empty()) return false;
    return load_source(vs.c_str(), fs.c_str());
}

bool Shader::load_source(const char* vert_src, const char* frag_src) {
    GLuint v = compile(GL_VERTEX_SHADER, vert_src);
    GLuint f = compile(GL_FRAGMENT_SHADER, frag_src);
    if (!v || !f) {
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        return false;
    }

    m_program = glCreateProgram();
    glAttachShader(m_program, v);
    glAttachShader(m_program, f);
    glLinkProgram(m_program);

    GLint ok;
    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(m_program, sizeof(log), nullptr, log);
        LOG_ERROR("Shader link error: %s", log);
        glDeleteProgram(m_program);
        m_program = 0;
    }

    glDeleteShader(v);
    glDeleteShader(f);
    return m_program != 0;
}

GLuint Shader::compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LOG_ERROR("Shader compile error (%s): %s",
                  type == GL_VERTEX_SHADER ? "vert" : "frag", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

void Shader::use() const { glUseProgram(m_program); }

void Shader::set_int(const char* name, int v) const {
    glUniform1i(glGetUniformLocation(m_program, name), v);
}
void Shader::set_float(const char* name, float v) const {
    glUniform1f(glGetUniformLocation(m_program, name), v);
}
void Shader::set_vec3(const char* name, const glm::vec3& v) const {
    glUniform3fv(glGetUniformLocation(m_program, name), 1, glm::value_ptr(v));
}
void Shader::set_vec4(const char* name, const glm::vec4& v) const {
    glUniform4fv(glGetUniformLocation(m_program, name), 1, glm::value_ptr(v));
}
void Shader::set_mat4(const char* name, const glm::mat4& m) const {
    glUniformMatrix4fv(glGetUniformLocation(m_program, name), 1, GL_FALSE, glm::value_ptr(m));
}

} // namespace mesh3d
