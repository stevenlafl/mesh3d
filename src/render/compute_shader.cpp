#include "render/compute_shader.h"
#include "util/log.h"
#include <fstream>
#include <sstream>
#include <glm/gtc/type_ptr.hpp>

namespace mesh3d {

ComputeShader::~ComputeShader() {
    if (m_program) glDeleteProgram(m_program);
}

ComputeShader::ComputeShader(ComputeShader&& o) noexcept : m_program(o.m_program) {
    o.m_program = 0;
}

ComputeShader& ComputeShader::operator=(ComputeShader&& o) noexcept {
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
        LOG_ERROR("Failed to open compute shader file: %s", path.c_str());
        return {};
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool ComputeShader::load(const std::string& comp_path) {
    std::string src = read_file(comp_path);
    if (src.empty()) return false;
    return load_source(src.c_str());
}

bool ComputeShader::load_source(const char* comp_src) {
    GLuint cs = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(cs, 1, &comp_src, nullptr);
    glCompileShader(cs);

    GLint ok;
    glGetShaderiv(cs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(cs, sizeof(log), nullptr, log);
        LOG_ERROR("Compute shader compile error: %s", log);
        glDeleteShader(cs);
        return false;
    }

    m_program = glCreateProgram();
    glAttachShader(m_program, cs);
    glLinkProgram(m_program);

    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(m_program, sizeof(log), nullptr, log);
        LOG_ERROR("Compute shader link error: %s", log);
        glDeleteProgram(m_program);
        m_program = 0;
    }

    glDeleteShader(cs);
    return m_program != 0;
}

void ComputeShader::use() const { glUseProgram(m_program); }

void ComputeShader::dispatch(GLuint groups_x, GLuint groups_y, GLuint groups_z) const {
    glDispatchCompute(groups_x, groups_y, groups_z);
}

void ComputeShader::set_int(const char* name, int v) const {
    glUniform1i(glGetUniformLocation(m_program, name), v);
}

void ComputeShader::set_ivec2(const char* name, int x, int y) const {
    glUniform2i(glGetUniformLocation(m_program, name), x, y);
}

void ComputeShader::set_float(const char* name, float v) const {
    glUniform1f(glGetUniformLocation(m_program, name), v);
}

void ComputeShader::set_vec3(const char* name, const glm::vec3& v) const {
    glUniform3fv(glGetUniformLocation(m_program, name), 1, glm::value_ptr(v));
}

} // namespace mesh3d
